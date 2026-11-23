#pragma once
// Shared by hsmono's sources, as MQ2MonoShared.h is by MQ2Mono's:
//   hsmono.cpp        the plugin's hooks, /mono, and the MonoCore.Core internal calls (MQ2Mono.cpp)
//   hsmono_imgui.cpp  the MonoCore.E3ImGUI internal calls (MQ2MonoImGui.cpp)
//   app_domains.cpp   loading and unloading scripts, and every call into C# (InitAppDomain, UnloadAppDomain)
//   mono_runtime.cpp  finding and starting Mono; mono_loader.cpp its OS calls
#include "hailstone_plugin.h"
#include "hailstone_plugin.hpp"
#include "mono_runtime.h"   // Mono's types, and its functions through `mono` (no import library is linked)
#include <stdlib.h>
#include <string.h>
#include <algorithm>    // std::equal in ci_equals
#include <array>        // std::array in m_IMGUI_InputColorValues
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>  // ci_equals' parameters

// The MQ2Mono version this port follows: /mono version, and mq_GetMQ2MonoVersion for the C# side.
inline constexpr const char* kHsMonoVersion = "0.426";

inline bool ci_equals(std::string_view a, std::string_view b) {
	auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
		[&](char x, char y) { return lower(x) == lower(y); });
}

// One running script: a C# assembly in an app domain of its own.
struct monoAppDomainInfo
{
	monoAppDomainInfo() = default;
	// Its commands capture `this`: it must stay where the map put it.
	monoAppDomainInfo(const monoAppDomainInfo&) = delete;
	monoAppDomainInfo& operator=(const monoAppDomainInfo&) = delete;

	std::string m_appDomainName;
	// app domain we have created for e3
	MonoDomain* m_appDomain = nullptr;
	// core.dll information so we can bind to it
	MonoAssembly* m_csharpAssembly = nullptr;
	MonoImage* m_coreAssemblyImage = nullptr;
	MonoClass* m_classInfo = nullptr;
	MonoObject* m_classInstance = nullptr;
	uint32_t m_classInstanceHandle = 0; // a pinned GC handle: keeps m_classInstance alive and where it is
	bool m_exceptionReported = false;   // the first exception out of C# is reported, then no more (OnPulse runs every frame)
	// methods that we call in C# if they are available
	MonoMethod* m_OnPulseMethod = nullptr;
	MonoMethod* m_OnSpawnAdd = nullptr;
	MonoMethod* m_OnZoned = nullptr;
	MonoMethod* m_OnWriteChatColor = nullptr;
	MonoMethod* m_OnIncomingChat = nullptr;
	MonoMethod* m_OnInit = nullptr;
	MonoMethod* m_OnUpdateImGui = nullptr;
	MonoMethod* m_OnStop = nullptr;
	MonoMethod* m_OnCommand = nullptr;
	MonoMethod* m_OnSetSpawns = nullptr;
	MonoMethod* m_OnSetSpawnsViaCallback = nullptr;
	MonoMethod* m_OnQuery = nullptr;

	std::map<std::string, bool> m_IMGUI_OpenWindows;
	std::map<std::string, bool> m_IMGUI_CheckboxValues;
	std::map<std::string, std::string> m_IMGUI_InputTextValues;
	std::map<std::string, std::array<float, 4>> m_IMGUI_InputColorValues;
	std::map<std::string, int> m_IMGUI_InputIntValues;
	std::map<std::string, bool> m_IMGUI_RadioButtonValues;
	std::string m_CurrentWindow;
	bool m_IMGUI_Open = true;
	int m_delayTime = 0; // amount of time in milliseconds that was set by C#
	std::chrono::steady_clock::time_point m_delayTimer = std::chrono::steady_clock::now(); // the time this was issued + m_delayTime
	std::deque<std::string> m_CommandList;

	// mq_AddCommand: a command that runs the script's OnCommand with the whole line. False when the name is taken
	// (the game's, the client's, another plugin's or another script's). app_domains.cpp.
	bool AddScriptCommand(const std::string& commandName);
	// mq_RemoveCommand, mq_ClearCommands; ClearCommands also when the script unloads.
	void RemoveCommand(const std::string& command);
	void ClearCommands();
};

// app_domains.cpp
extern std::map<std::string, monoAppDomainInfo> monoAppDomains;
extern std::map<MonoDomain*, std::string> monoAppDomainPtrToString;
extern std::deque<std::string> appDomainProcessQueue; // a revolving list of who is valid to process
// hsmono.cpp
extern std::atomic<bool> previousCommand; // a command was issued on this pulse (set from a script's thread too)

// ---- app_domains.cpp

// Calls one of a script's methods in the script's domain, then makes the domain that was current before current again
// (a script's command may run inside another script's call). An exception thrown in C# is caught and reported here
// (`what` names the method), never left to Mono's unhandled-exception handler. Empty when it threw, else the method's
// result (null for void). A null method is not called.
std::optional<MonoObject*> Invoke(monoAppDomainInfo& script, MonoMethod* method, const char* what, void** params = nullptr);

// The script whose code is running (mono_domain_get), for the internal calls: null from the root domain.
monoAppDomainInfo* CurrentScript();

// True while C# code is running: a script can't be loaded or unloaded then, since its domain may be the one running.
bool InScriptCall();

// True while the client's thread is lent to the scripts' own threads (lend_thread, around the outermost Invoke): a
// script's thread may call the host then. False on a client without lend_thread.
bool ClientThreadLent();

// A name the player typed, as a script name (letters, digits, '_', '-', '.'; a trailing ".dll" dropped), or empty.
std::optional<std::string> ScriptName(std::string_view typed);

// /mono load: copies <plugin folder>/macros/<name>/ into its <character>/ folder and loads <name>.dll from there into a
// new app domain (InitAppDomain). A running script of that name is unloaded first. In game only. Empty on success.
std::optional<std::string> LoadScript(const std::string& name);

// /mono unload: OnStop, its commands, its domain (UnloadAppDomain). False when no such script runs.
bool UnloadScript(std::string_view name);
void UnloadAllScripts();

// ${HSMono.Query[name,expression]}: the named script's OnQuery(expression) (any case of the name, as /mono). Empty
// when no such script runs, it has no OnQuery, it threw (reported once, as any call) or it returned null.
std::string QueryScript(std::string_view name, const std::string& expression);

// ---- internal calls

// A C# method declared [MethodImpl(MethodImplOptions.InternalCall)] runs the C++ function registered for its
// "Namespace.Class::Method". Mono takes the function as const void*, which C++ does not convert to on its own.
#define HSMONO_ICALL(name, fn) mono.mono_add_internal_call(name, reinterpret_cast<const void*>(&fn))

// hsmono_imgui.cpp: the MonoCore.E3ImGUI internal calls, from RegisterInternalCalls.
void RegisterImGuiInternalCalls();
// hsmono_imgui.cpp: each script's OnUpdateImGui, in the client's ImGui context (the plugin's OnUpdateImGui).
void RunScriptsImGui();
