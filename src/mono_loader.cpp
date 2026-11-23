// Every OS call hsmono makes: loading the Mono runtime, finding an installed one, and the process's memory.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
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

// The process's private memory in bytes, 0 when the OS won't say: what Windows calls its page file usage (the private
// bytes committed, PROCESS_MEMORY_COUNTERS::PagefileUsage). Linux and macOS have no commit charge, so the nearest:
// Linux the private (anonymous) memory in RAM and swapped out (/proc/self/status RssAnon + VmSwap), macOS the physical
// footprint (TASK_VM_INFO phys_footprint, Activity Monitor's "Memory").
std::uint64_t PrivateMemoryBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof counters;
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) return 0;
    return counters.PagefileUsage;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
#else
    std::ifstream status("/proc/self/status");
    std::uint64_t kib = 0;
    for (std::string line; std::getline(status, line);) {
        if (line.rfind("RssAnon:", 0) == 0 || line.rfind("VmSwap:", 0) == 0)
            kib += std::strtoull(line.c_str() + line.find(':') + 1, nullptr, 10);   // "RssAnon:   12345 kB"
    }
    return kib * 1024;
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
