#include "mono_runtime.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "hailstone_plugin.h"

// mono_loader.cpp: the OS calls.
void* OpenLibrary(const std::filesystem::path& file, std::string& error);
using AnyFn = void (*)();
AnyFn FindSymbol(void* library, const char* name);
#if defined(_WIN32)
std::vector<std::filesystem::path> WindowsMonoRoots();
#endif

MonoApi mono;
MonoDomain* g_rootDomain = nullptr;

namespace fs = std::filesystem;

namespace {

// The library's name in a runtime folder of the add-on's (bundled, or hsmono.ini's monoRuntimeDir).
#if defined(_WIN32)
    constexpr const char* kMonoLibrary = "mono-2.0-sgen.dll";
#elif defined(__APPLE__)
    constexpr const char* kMonoLibrary = "libmonosgen-2.0.dylib";
#else
    constexpr const char* kMonoLibrary = "libmonosgen-2.0.so";
#endif

    // Mono takes UTF-8 paths on every platform.
    std::string Utf8(const fs::path& p) {
        const auto u = p.u8string();
        return std::string(reinterpret_cast<const char*>(u.data()), u.size());
    }

    // host->config_dir: <Hailstone folder>/plugins/hsmono/.
    // NOTE: the client currently returns it in the system's narrow encoding (path.string(): the ANSI code page on
    // Windows), so a plain fs::path(string) reads it back right. If the client switches to UTF-8, build it from
    // std::u8string instead.
    fs::path ConfigDir() {
        char buf[4096];
        const int n = host ? host->config_dir(host->host, buf, sizeof buf) : 0;
        return n > 0 && static_cast<size_t>(n) < sizeof buf ? fs::path(std::string(buf, static_cast<size_t>(n))) : fs::path();
    }

    // monoRuntimeDir=<folder> in hsmono.ini, if the player set one.
    std::optional<fs::path> IniRuntimeDir(const fs::path& configDir) {
        std::ifstream ini(configDir / "hsmono.ini");
        for (std::string line; std::getline(ini, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            constexpr std::string_view key = "monoRuntimeDir=";
            if (line.rfind(key, 0) == 0 && line.size() > key.size()) {
                const std::string value = line.substr(key.size());
                return fs::path(std::u8string(value.begin(), value.end()));   // the ini is UTF-8
            }
        }
        return std::nullopt;
    }

    bool IsFile(const fs::path& p) {
        std::error_code ec;   // nothing may throw out of a command
        return fs::is_regular_file(p, ec);   // follows symlinks: Homebrew's and the distros' libraries are links
    }

    // One place a Mono runtime may be.
    struct Candidate {
        fs::path library;               // a full path; on Linux also a bare soname, for the dynamic linker to find
        std::optional<fs::path> root;   // set: mono_set_dirs(root/lib, root/etc); unset: the runtime's built-in prefix
        const char* source;             // for messages
    };

    // In order. The player's hsmono.ini choice is the only one when set. Then the add-on's own copy, so a player gets
    // the Mono the add-on was tested with even when another one is installed. Then an installed Mono.
    std::vector<Candidate> Candidates(const fs::path& config) {
        std::vector<Candidate> out;
        if (!config.empty()) {
            if (std::optional<fs::path> custom = IniRuntimeDir(config)) {
                out.push_back({*custom / kMonoLibrary, *custom, "hsmono.ini monoRuntimeDir"});
                return out;
            }
            out.push_back({config / "mono" / kMonoLibrary, config / "mono", "bundled"});
        }
#if defined(_WIN32)
        // The installer's layout is known (bin\, lib\mono\, etc\mono\), so its folders are given to Mono outright.
        for (const fs::path& root : WindowsMonoRoots())
            out.push_back({root / "bin" / kMonoLibrary, root, "installed"});
#elif defined(__APPLE__)
        // Full paths: a bare name on macOS searches only ~/lib, /usr/local/lib and /usr/lib. A packaged Mono knows its
        // own prefix, so no mono_set_dirs.
        for (const char* p : {
                 "/Library/Frameworks/Mono.framework/Versions/Current/lib/libmonosgen-2.0.dylib",   // the official installer
                 "/opt/homebrew/lib/libmonosgen-2.0.dylib",                                         // Homebrew, Apple Silicon
                 "/usr/local/lib/libmonosgen-2.0.dylib",                                            // Homebrew, Intel
             })
            out.push_back({p, std::nullopt, "installed"});
#else
        // The versioned soname: the unversioned .so usually comes only with the -dev package. The dynamic linker finds
        // it wherever the distribution put it (/usr/lib, /usr/lib64, a multiarch folder ...), and the packaged Mono
        // knows its own prefix, so no mono_set_dirs.
        out.push_back({"libmonosgen-2.0.so.1", std::nullopt, "installed"});
#endif
        return out;
    }

    // Why a candidate can't be used, checked without loading anything: Mono aborts the process on some setup errors.
    std::optional<std::string> Unusable(const Candidate& c) {
        if (c.library.has_parent_path() && !IsFile(c.library)) return std::string("not found");
        if (c.root) {
            const fs::path corlib = *c.root / "lib" / "mono" / "4.5" / "mscorlib.dll";
            if (!IsFile(corlib)) return "no class libraries (" + Utf8(corlib) + ")";
            const fs::path config = *c.root / "etc" / "mono" / "config";
            if (!IsFile(config)) return "no config (" + Utf8(config) + ")";
        }
        return std::nullopt;
    }

    const char* BindAll(void* library) {
#define HSMONO_BIND(f)                                                    \
    mono.f = reinterpret_cast<decltype(mono.f)>(FindSymbol(library, #f)); \
    if (!mono.f) return #f;
        HSMONO_FUNCTIONS(HSMONO_BIND)
#undef HSMONO_BIND
        return nullptr;
    }

} // namespace

// The add-on's runtime folder: hsmono.ini's monoRuntimeDir, else <plugin folder>/mono. An installed Mono found by
// InitMono may be elsewhere.
fs::path MonoRuntimeDir() {
    const fs::path config = ConfigDir();
    if (auto custom = IniRuntimeDir(config)) return *custom;
    return config / "mono";
}

fs::path PluginConfigDir() { return ConfigDir(); }

std::optional<std::string> InitMono() {
    static bool started = false;
    static std::optional<std::string> failure;   // after the JIT started, a failure is final until restart
    if (started) return failure;

    std::string tried;
    for (const Candidate& c : Candidates(ConfigDir())) {
        const std::string where = std::string(c.source) + " " + Utf8(c.library);
        if (std::optional<std::string> why = Unusable(c)) {
            tried += "\n  " + where + ": " + *why;
            continue;
        }
        std::string error;
        void* handle = OpenLibrary(c.library, error);   // never unloaded: Mono cannot be stopped and started again
        if (!handle) {
            tried += "\n  " + where + ": did not load (" + error + ")";
            continue;
        }

        // From here on this candidate is the one: a second Mono in the same process would not work.
        if (const char* missing = BindAll(handle)) return where + " is missing " + missing;

        // Loaded again after /plugin unload: the runtime is still up in this process, so join it.
        if (MonoDomain* existing = mono.mono_get_root_domain()) {
            g_rootDomain = existing;
            started = true;
            RegisterInternalCalls();   // the new plugin copy's functions replace the old copy's
            return std::nullopt;
        }

        started = true;
        if (c.root) {
            // mono_set_dirs keeps these pointers: they must outlive the runtime.
            static std::string libDir8, etcDir8;
            libDir8 = Utf8(*c.root / "lib");
            etcDir8 = Utf8(*c.root / "etc");
            mono.mono_set_dirs(libDir8.c_str(), etcDir8.c_str());
        }
        mono.mono_config_parse(nullptr);
        g_rootDomain = mono.mono_jit_init_version("Mono_Domain", "v4.0.30319");
        if (!g_rootDomain) return failure = "mono_jit_init failed with " + where + "; restart Hailstone to try again";

        RegisterInternalCalls();   // once, before any assembly loads
        char* build = mono.mono_get_runtime_build_info();   // an installed Mono may not be the version you tested
        WriteChatf("[hsmono] Mono %s: %s", build ? build : "(unknown version)", where.c_str());
        if (build) mono.mono_free(build);
        return std::nullopt;
    }
    return "No usable Mono runtime. Tried:" + tried;
}
