#pragma once
// The Mono runtime, loaded at run time from wherever InitMono finds it (mono_runtime.cpp). hsmono links no Mono import
// library and needs no Mono beside Hailstone's executable: every Mono function is called through `mono`, e.g.
//     mono.mono_domain_set(domain, false);
// A direct call (mono_domain_set(...)) would need the import library and fails to link.

#include <filesystem>
#include <optional>
#include <string>

// Mono's own headers, for its types and for the functions' signatures only (decltype below).
#include <mono/jit/jit.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/class.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/object.h>
#include <mono/metadata/threads.h>
#include <mono/utils/mono-publib.h>

// Every Mono function hsmono calls. To call another, add it here: its signature comes from Mono's headers. InitMono
// refuses a runtime that lacks any of them, so add only what hsmono really calls.
#define HSMONO_FUNCTIONS(X)              \
    /* starting the runtime */           \
    X(mono_set_dirs)                     \
    X(mono_config_parse)                 \
    X(mono_jit_init_version)             \
    X(mono_get_root_domain)              \
    X(mono_get_runtime_build_info)       \
    X(mono_add_internal_call)            \
    X(mono_thread_attach)                \
    /* app domains */                    \
    X(mono_domain_get)                   \
    X(mono_domain_set)                   \
    X(mono_domain_create_appdomain)      \
    X(mono_domain_unload)                \
    X(mono_domain_set_config)            \
    X(mono_domain_assembly_open)         \
    /* classes, objects and calls */     \
    X(mono_assembly_get_image)           \
    X(mono_class_from_name)              \
    X(mono_class_get_method_from_name)   \
    X(mono_object_new)                   \
    X(mono_runtime_invoke)               \
    X(mono_gchandle_new)                 \
    X(mono_gchandle_free)                \
    /* arrays */                         \
    X(mono_get_int32_class)              \
    X(mono_get_single_class)             \
    X(mono_array_new)                    \
    X(mono_array_addr_with_size)         \
    /* strings */                        \
    X(mono_string_new)                   \
    X(mono_string_to_utf8)               \
    X(mono_free)

struct MonoApi {
#define HSMONO_POINTER(f) decltype(&::f) f = nullptr;
    HSMONO_FUNCTIONS(HSMONO_POINTER)
#undef HSMONO_POINTER
};

// Filled by InitMono; all null before it succeeds.
extern MonoApi mono;
// The root domain, once InitMono has succeeded.
extern MonoDomain* g_rootDomain;

// Finds, loads and starts Mono the first time it is called (the first /mono): hsmono.ini's monoRuntimeDir, else the
// add-on's bundled <plugin folder>/mono/, else an installed Mono. Empty when Mono is running, else a message for the
// player saying what was tried. Call it on the client's thread.
std::optional<std::string> InitMono();

// Every mono.mono_add_internal_call(...): the C# side's [MethodImpl(InternalCall)] methods and the C++ functions behind
// them. In hsmono.cpp, beside those functions. InitMono calls it once after the runtime starts, and again when the
// plugin is loaded again into a process where Mono is still up (the new plugin copy's functions replace the old's).
void RegisterInternalCalls();

// The add-on's runtime folder: hsmono.ini's monoRuntimeDir, else <plugin folder>/mono. The Mono InitMono uses may be an
// installed one elsewhere.
std::filesystem::path MonoRuntimeDir();

// The plugin's own folder (host->config_dir): <Hailstone folder>/plugins/hsmono/. Empty when the client gives none.
std::filesystem::path PluginConfigDir();

// A path as UTF-8, the encoding Mono takes on every platform.
inline std::string PathUtf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
