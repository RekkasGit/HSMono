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

// The library's name in a runtime folder of the add-on's (bundled, or hsmono.ini's monodirectory).
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

    // hsmono.ini's monodirectory, as ReadSettings last read it (InitMono, before it looks for Mono); unset when empty or
    // not there.
    std::optional<fs::path> iniMonoDirectory;

    // Spaces and tabs off both ends.
    std::string_view Trim(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    }

    bool KeyIs(std::string_view key, std::string_view name) {
        if (key.size() != name.size()) return false;
        for (size_t i = 0; i < key.size(); ++i) {
            const char c = key[i] >= 'A' && key[i] <= 'Z' ? static_cast<char>(key[i] - 'A' + 'a') : key[i];
            if (c != name[i]) return false;
        }
        return true;
    }

    std::optional<fs::path> IniRuntimeDir() { return iniMonoDirectory; }

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
            if (std::optional<fs::path> custom = IniRuntimeDir()) {
                out.push_back({*custom / kMonoLibrary, *custom, "hsmono.ini monodirectory"});
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

    // hsmono.ini's monodirectory: read by InitMono each time it tries to start Mono, so an edit made after the plugin
    // loaded counts until Mono is running. Key names take any case; a line starting with ';' or '#', or a [section]
    // line, is skipped. An empty value is no folder.
    void ReadSettings() {
        iniMonoDirectory.reset();
        const fs::path config = ConfigDir();
        if (config.empty()) return;
        std::ifstream ini(config / "hsmono.ini", std::ios::binary);
        for (std::string raw; std::getline(ini, raw);) {
            const std::string_view line = Trim(raw);
            if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') continue;
            const size_t eq = line.find('=');
            if (eq == std::string_view::npos || !KeyIs(Trim(line.substr(0, eq)), "monodirectory")) continue;
            const std::string_view value = Trim(line.substr(eq + 1));
            if (value.empty()) iniMonoDirectory.reset();
            else iniMonoDirectory = fs::path(std::u8string(value.begin(), value.end()));   // the ini is UTF-8
        }
    }

} // namespace

// hsmono.ini in the plugin's folder, made with an empty monodirectory= when it isn't there. It is only read when Mono
// starts (ReadSettings, from InitMono).
void EnsureSettingsFile() {
    const fs::path config = ConfigDir();
    if (config.empty()) return;
    const fs::path file = config / "hsmono.ini";
    std::error_code ec;   // nothing may throw out of InitializePlugin
    if (fs::exists(file, ec) || ec) return;
    std::ofstream out(file, std::ios::binary);
    out << "; HSMono's settings, read when the first script is loaded (the first /mono load, which starts Mono).\n"
           "; monodirectory: a Mono runtime folder to use instead of the add-on's own (<plugin folder>/mono) or an\n"
           "; installed Mono; it holds the runtime library and the lib/ and etc/ folders Mono is configured from.\n"
           "monodirectory=\n";
}

// The add-on's runtime folder: hsmono.ini's monodirectory, else <plugin folder>/mono. An installed Mono found by
// InitMono may be elsewhere.
fs::path MonoRuntimeDir() {
    const fs::path config = ConfigDir();
    if (auto custom = IniRuntimeDir()) return *custom;
    return config / "mono";
}

fs::path PluginConfigDir() { return ConfigDir(); }

std::optional<std::string> InitMono() {
    static bool started = false;
    static std::optional<std::string> failure;   // after the JIT started, a failure is final until restart
    if (started) return failure;

    ReadSettings();   // hsmono.ini's monodirectory, read now: nothing of Mono is loaded before the first script
    // What was configured and tried, in order: in chat with the result either way, so a player can see which Mono
    // was taken and why the ones before it were not.
    const std::string configured = iniMonoDirectory ? "hsmono.ini monodirectory: " + Utf8(*iniMonoDirectory)
                                                    : std::string("hsmono.ini monodirectory: not set");
    std::vector<std::string> skipped;   // "<source> <library>: <why>", for each candidate passed over
    auto report = [&](const std::string& chosen) {
        WriteChatf("[HSMono] %s", configured.c_str());
        for (const std::string& line : skipped) WriteChatf("[HSMono] skipped %s", line.c_str());
        WriteChatf("[HSMono] %s", chosen.c_str());
    };
    for (const Candidate& c : Candidates(ConfigDir())) {
        const std::string where = std::string(c.source) + " " + Utf8(c.library);
        if (std::optional<std::string> why = Unusable(c)) {
            skipped.push_back(where + ": " + *why);
            continue;
        }
        std::string error;
        void* handle = OpenLibrary(c.library, error);   // never unloaded: Mono cannot be stopped and started again
        if (!handle) {
            skipped.push_back(where + ": did not load (" + error + ")");
            continue;
        }

        // From here on this candidate is the one: a second Mono in the same process would not work.
        if (const char* missing = BindAll(handle)) return configured + "\n" + where + " is missing " + missing;

        // Loaded again after /plugin unload: the runtime is still up in this process, so join it.
        if (MonoDomain* existing = mono.mono_get_root_domain()) {
            g_rootDomain = existing;
            started = true;
            report("using " + where + " (the runtime already running in this process)");
            RegisterInternalCalls();   // the new plugin copy's functions replace the old copy's
            return std::nullopt;
        }

        started = true;
        const std::string dirs = c.root ? "Mono's folders: lib " + Utf8(*c.root / "lib") + ", etc " + Utf8(*c.root / "etc")
                                        : std::string("Mono's folders: the runtime's own prefix (no mono_set_dirs)");
        if (c.root) {
            // mono_set_dirs keeps these pointers: they must outlive the runtime.
            static std::string libDir8, etcDir8;
            libDir8 = Utf8(*c.root / "lib");
            etcDir8 = Utf8(*c.root / "etc");
            mono.mono_set_dirs(libDir8.c_str(), etcDir8.c_str());
        }
        mono.mono_config_parse(nullptr);
        g_rootDomain = mono.mono_jit_init_version("Mono_Domain", "v4.0.30319");
        if (!g_rootDomain)
            return failure = configured + "\n" + dirs + "\nmono_jit_init failed with " + where +
                             "; restart Hailstone to try again";

        RegisterInternalCalls();   // once, before any assembly loads
        report("using " + where);
        WriteChatf("[HSMono] %s", dirs.c_str());
        char* build = mono.mono_get_runtime_build_info();   // an installed Mono may not be the version you tested
        WriteChatf("[HSMono] Mono %s", build ? build : "(unknown version)");
        if (build) mono.mono_free(build);
        return std::nullopt;
    }
    std::string message = configured + "\nNo usable Mono runtime. Tried:";
    for (const std::string& line : skipped) message += "\n  " + line;
    return message;
}
