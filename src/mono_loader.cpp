// Every OS call hsmono makes: loading the Mono runtime and finding an installed one.
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using AnyFn = void (*)();

// On Windows, `file` must be a full path. The library's own folder serves its dependencies; PATH and the current folder
// never do (Hailstone sets that for the whole process).
// On Linux and macOS a bare name ("libmonosgen-2.0.so.1") is looked up by the dynamic linker's own search, the way an
// installed program finds its libraries; a name with a '/' is loaded from there only.
void* OpenLibrary(const std::filesystem::path& file, std::string& error) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        const DWORD e = GetLastError();
        error = e == ERROR_BAD_EXE_FORMAT ? "it is not a 64-bit library (error 193)"
            : e == ERROR_MOD_NOT_FOUND ? "a library it needs is missing (error 126)"
            : "Windows error " + std::to_string(e);
    }
    return h;
#else
    void* h = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        error = e ? e : "unknown error";
    }
    return h;
#endif
}

AnyFn FindSymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<AnyFn>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    void* p = dlsym(library, name);
    AnyFn fn = nullptr;
    static_assert(sizeof fn == sizeof p);
    std::memcpy(&fn, &p, sizeof fn);   // object pointer to function pointer without a conditionally-supported cast
    return fn;
#endif
}

#if defined(_WIN32)
namespace {

std::wstring RegistryString(HKEY root, const wchar_t* key, const wchar_t* value) {
    DWORD bytes = 0;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0) return {};
    std::wstring s(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, s.data(), &bytes) != ERROR_SUCCESS) return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

std::wstring Environment(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring s(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, s.data(), n);
    s.resize(got < n ? got : 0);
    return s;
}

} // namespace

// Where an installed 64-bit Mono may be, most likely first: the folder that holds bin\, lib\mono\ and etc\mono\.
// Not verified against every installer version: the registry value is the one the official installer is documented
// to write, and Program Files\Mono is its default folder. A 64-bit process reads the 64-bit registry view, which is
// the 64-bit Mono's.
std::vector<std::filesystem::path> WindowsMonoRoots() {
    std::vector<std::filesystem::path> out;
    auto add = [&](const std::filesystem::path& p) {
        if (p.empty()) return;
        for (const auto& q : out)
            if (q == p) return;
        out.push_back(p);
    };
    add(RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Mono", L"SdkInstallRoot"));
    if (const std::wstring pf = Environment(L"ProgramFiles"); !pf.empty()) add(std::filesystem::path(pf) / L"Mono");
    return out;
}
#endif
