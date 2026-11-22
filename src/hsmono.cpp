/* An example Hailstone plugin in MacroQuest's style (sdk/hailstone_plugin.h): a /hello command, an
 * ${Example} TLO, the pulse, zoning, chat and packet callbacks, and a web page of its own (/webexample, ABI 3).
 * Built with the client (CMake target HSMono); a server with a hailstone.cfg may keep the player's own plugins
 * off (docs/plugins.md). */

#include "hsmono.hpp"

HAILSTONE_PLUGIN

static int pulses, chats, zones;
static uint32_t page; /* the plugin's web tab (0: none) */
bool previousCommand = false; //a command was issued on this call.
//used to keep a revolving list of who is valid to process. 
std::deque<std::string> appDomainProcessQueue;
std::map<std::string, monoAppDomainInfo> monoAppDomains;
std::map<MonoDomain*, std::string> monoAppDomainPtrToString;
MonoDomain* _rootDomain;
int _milisecondsToProcess = 8;
static std::chrono::steady_clock::time_point PulseTimer = std::chrono::steady_clock::now() + std::chrono::seconds(5);


//static void hello(void* user, const char* args) {
//    (void)user;
//    WriteChatf("[HSMono] Hello, %s!", args && *args ? args : "world");
//}

/* /webexample <address>: opens the plugin's own tab on it (or moves it there); /webexample post <text>: hands the
 * text to the page's window.hailstone.onmessage. What the page posts (window.hailstone.post) comes to the chat window
 * in OnPulse. A window would draw the tab with host->web_view in OnUpdateImGui. Needs the server's 'web'
 * permission, and the address must be on a site its hailstone.cfg allows. */
//static void webexample(void* user, const char* args) {
//   
//}

/* ${Example}, ${Example.Pulses}, ${Example.Chats}, ${Example.Zones}, ${Example[x].Index}, ${Example.Twice[21]},
 * ${Example.MyHP} (the plugin reading a TLO of the client's) */
//static int example_tlo(void* user, const char* index, const char* member, const char* member_index, HailstoneValue* out) {
//    static char hp[32];
//    (void)user;
//    if (!member) {
//        out->kind = HAILSTONE_STRING;
//        out->s = "Example";
//        return 1;
//    }
//    if (strcmp(member, "Pulses") == 0) {
//        out->kind = HAILSTONE_INT;
//        out->i = pulses;
//        return 1;
//    }
//    if (strcmp(member, "Chats") == 0) {
//        out->kind = HAILSTONE_INT;
//        out->i = chats;
//        return 1;
//    }
//    if (strcmp(member, "Zones") == 0) {
//        out->kind = HAILSTONE_INT;
//        out->i = zones;
//        return 1;
//    }
//    if (strcmp(member, "Index") == 0) {
//        out->kind = HAILSTONE_STRING;
//        out->s = index;
//        return 1;
//    }
//    if (strcmp(member, "Twice") == 0) {
//        out->kind = HAILSTONE_INT;
//        out->i = 2 * atoi(member_index);
//        return 1;
//    }
//    if (strcmp(member, "MyHP") == 0) {
//        if (ParseMacroData("${Me.PctHPs}", hp, sizeof hp) < 0) return 0;
//        out->kind = HAILSTONE_INT;
//        out->i = atoi(hp);
//        return 1;
//    }
//    return 0;
//}


// ---- /mono <name>: starts Mono if it isn't running, then loads <plugin folder>/macros/<name>/<name>.dll into an app
// domain of its own. /mono unload <name> stops it again.

namespace {

namespace fs = std::filesystem;

// The C# side's entry class: namespace MonoCore, class Core, as in MQ2Mono.
constexpr const char* kCoreNamespace = "MonoCore";
constexpr const char* kCoreClass = "Core";

void ChatLines(const std::string& text) {   // one chat line per '\n': InitMono's message lists every place it tried
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        WriteChatf("[HSMono] %s", text.substr(start, end == std::string::npos ? std::string::npos : end - start).c_str());
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// A script's name becomes a folder and a file name: letters, digits, '_', '-' and '.' only, so it can't leave macros/.
// A trailing ".dll" is dropped ("/mono e3.dll" is "/mono e3").
std::optional<std::string> ScriptName(std::string_view s) {
    if (s.size() > 4 && ci_equals(s.substr(s.size() - 4), ".dll")) s.remove_suffix(4);
    if (s.empty() || s.size() > 64 || s.find("..") != std::string_view::npos) return std::nullopt;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) return std::nullopt;
    }
    return std::string(s);
}

std::map<std::string, monoAppDomainInfo>::iterator FindScript(std::string_view name) {
    for (auto it = monoAppDomains.begin(); it != monoAppDomains.end(); ++it)
        if (ci_equals(it->first, name)) return it;
    return monoAppDomains.end();
}

// Calls one of the script's methods in its own domain. False when it threw.
bool Invoke(monoAppDomainInfo& d, MonoMethod* method, void** params) {
    if (!method) return true;
    MonoObject* exception = nullptr;
    mono.mono_domain_set(d.m_appDomain, false);
    mono.mono_runtime_invoke(method, d.m_classInstance, params, &exception);
    return exception == nullptr;
}

// Stops a script: its OnStop, its commands, then its domain. Not from inside one of that script's own calls (a C#
// DoCommand("/mono unload ...") of itself): the domain would be unloaded under the running code.
void UnloadScript(std::map<std::string, monoAppDomainInfo>::iterator it) {
    monoAppDomainInfo& d = it->second;
    if (!Invoke(d, d.m_OnStop, nullptr)) WriteChatf("[HSMono] %s: OnStop threw an exception", it->first.c_str());
    for (const std::string& command : d.m_CommandList) hs::RemoveCommand(command);
    d.ClearCommands();
    d.commands_.clear();

    MonoDomain* domain = d.m_appDomain;
    if (d.m_classInstanceHandle) mono.mono_gchandle_free(d.m_classInstanceHandle);
    std::erase(appDomainProcessQueue, it->first);
    monoAppDomainPtrToString.erase(domain);
    monoAppDomains.erase(it);

    mono.mono_domain_set(g_rootDomain, false);   // a domain can't be unloaded while it is the current one
    if (domain) mono.mono_domain_unload(domain);
}

std::optional<std::string> LoadScript(const std::string& name) {
    if (FindScript(name) != monoAppDomains.end()) return name + " is already running";

    const fs::path dll = PluginConfigDir() / "macros" / name / (name + ".dll");
    std::error_code ec;   // nothing may throw out of a command
    if (!fs::is_regular_file(dll, ec)) return "not found: " + PathUtf8(dll);

    std::string friendlyName = name;   // Mono's signature takes char*
    MonoDomain* domain = mono.mono_domain_create_appdomain(friendlyName.data(), nullptr);
    if (!domain) return "could not create an app domain for " + name;
    auto fail = [&](std::string why) {
        mono.mono_domain_set(g_rootDomain, false);
        mono.mono_domain_unload(domain);
        return std::optional<std::string>(std::move(why));
    };

    mono.mono_domain_set(domain, false);
    const std::string dll8 = PathUtf8(dll);
    MonoAssembly* assembly = mono.mono_domain_assembly_open(domain, dll8.c_str());
    if (!assembly) return fail("could not load " + dll8);
    MonoImage* image = mono.mono_assembly_get_image(assembly);
    MonoClass* cls = image ? mono.mono_class_from_name(image, kCoreNamespace, kCoreClass) : nullptr;
    if (!cls) return fail(name + ".dll has no class " + kCoreNamespace + "." + kCoreClass);
    MonoObject* instance = mono.mono_object_new(domain, cls);
    if (!instance) return fail("could not create " + std::string(kCoreNamespace) + "." + kCoreClass);
    // Pinned before anything else runs in the domain: the GC may move or collect an object only C++ refers to.
    const uint32_t handle = mono.mono_gchandle_new(instance, true);
    mono.mono_runtime_object_init(instance);   // the constructor

    monoAppDomainInfo& d = monoAppDomains.try_emplace(name).first->second;
    d.m_appDomainName = name;
    d.m_appDomain = domain;
    d.m_csharpAssembly = assembly;
    d.m_coreAssemblyImage = image;
    d.m_classInfo = cls;
    d.m_classInstance = instance;
    d.m_classInstanceHandle = handle;
    // Any parameter count (-1): the C# side decides each method's signature, and one it lacks stays null.
    auto method = [cls](const char* n) { return mono.mono_class_get_method_from_name(cls, n, -1); };
    d.m_OnPulseMethod = method("OnPulse");
    d.m_OnSpawnAdd = method("OnSpawnAdd");
    d.m_OnZoned = method("OnZoned");
    d.m_OnWriteChatColor = method("OnWriteChatColor");
    d.m_OnIncomingChat = method("OnIncomingChat");
    d.m_OnInit = method("OnInit");
    d.m_OnUpdateImGui = method("OnUpdateImGui");
    d.m_OnStop = method("OnStop");
    d.m_OnCommand = method("OnCommand");
    d.m_OnSetSpawns = method("OnSetSpawns");
    d.m_OnSetSpawnsViaCallback = method("OnSetSpawnsViaCallback");
    d.m_OnQuery = method("OnQuery");

    monoAppDomainPtrToString[domain] = name;
    appDomainProcessQueue.push_back(name);

    if (!Invoke(d, d.m_OnInit, nullptr)) {
        UnloadScript(monoAppDomains.find(name));
        return name + ": OnInit threw an exception; unloaded";
    }
    mono.mono_domain_set(g_rootDomain, false);
    return std::nullopt;
}

void MonoCommand(const char* args) {
    const std::string_view line = Trim(args);
    if (line.empty()) {
        WriteChatf("[HSMono] /mono <name> loads <plugin folder>/macros/<name>/<name>.dll; /mono unload <name> stops it");
        for (const auto& [name, info] : monoAppDomains) WriteChatf("[HSMono] running: %s", name.c_str());
        return;
    }

    std::string_view first = line.substr(0, line.find_first_of(" \t"));
    const bool unload = ci_equals(first, "unload");
    const std::string_view rest = unload ? Trim(line.substr(first.size())) : line;
    const std::optional<std::string> name = ScriptName(rest);
    if (!name) {
        WriteChatf("[HSMono] not a script name: '%.*s'", static_cast<int>(rest.size()), rest.data());
        return;
    }

    if (unload) {
        auto it = FindScript(*name);
        if (it == monoAppDomains.end() || !g_rootDomain) {
            WriteChatf("[HSMono] %s is not running", name->c_str());
            return;
        }
        const std::string shown = it->first;
        UnloadScript(it);
        WriteChatf("[HSMono] %s unloaded", shown.c_str());
        return;
    }

    if (std::optional<std::string> error = InitMono()) {   // the first /mono starts the runtime
        ChatLines(*error);
        return;
    }
    if (std::optional<std::string> error = LoadScript(*name)) {
        ChatLines(*error);
        return;
    }
    WriteChatf("[HSMono] %s loaded", name->c_str());
}

} // namespace

// A C# method declared [MethodImpl(MethodImplOptions.InternalCall)] runs the C++ function registered for its
// "Namespace.Class::Method". Mono takes the function as const void*, which C++ does not convert to on its own.
#define HSMONO_ICALL(name, fn) mono.mono_add_internal_call(name, reinterpret_cast<const void*>(&fn))

// mono_runtime.h. The namespace and classes are fixed by the C# side: MonoCore.Core and MonoCore.E3ImGUI.
// Each line stays commented out until its mono_* function is in this file (above this point, or declared there).
void RegisterInternalCalls() {
    // HSMONO_ICALL("MonoCore.Core::mq_Echo", mono_Echo);
    // HSMONO_ICALL("MonoCore.Core::mq_ParseTLO", mono_ParseTLO);
    // HSMONO_ICALL("MonoCore.Core::mq_DoCommand", mono_DoCommand);
    // HSMONO_ICALL("MonoCore.Core::mq_DoCommandDelayed", mono_DoCommandDelayed);
    // HSMONO_ICALL("MonoCore.Core::mq_Delay", mono_Delay);
    // HSMONO_ICALL("MonoCore.Core::mq_AddCommand", mono_AddCommand);
    // HSMONO_ICALL("MonoCore.Core::mq_ClearCommands", mono_ClearCommands);
    // HSMONO_ICALL("MonoCore.Core::mq_RemoveCommand", mono_RemoveCommand);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpellDataEffectCount", mono_GetSpellDataEffectCount);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpellDataEffect", mono_GetSpellDataEffect);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns", mono_GetSpawns);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns2", mono_GetSpawns2);

    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns3", mono_GetSpawns3);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns3_Buffer", mono_GetSpawns3Buffer);
    // HSMONO_ICALL("MonoCore.Core::mq_GetOnAddSpawnsBuffer", mono_GetOnAddSpawnsBuffer);

    // HSMONO_ICALL("MonoCore.Core::mq_GetBuffData", mono_GetBuffData);
    // HSMONO_ICALL("MonoCore.Core::mq_GetPetBuffData", mono_GetPetBuffData);
    // HSMONO_ICALL("MonoCore.Core::mq_GetTargetBuffData", mono_GetTargetBuffData);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns3_Delta", mono_GetSpawns3_Delta);
    // HSMONO_ICALL("MonoCore.Core::mq_GetXtargetInfo", mono_GetXtargetInfo);
    // HSMONO_ICALL("MonoCore.Core::mq_GetAvailableAAIds", mono_GetAvailableAAIds);
    // HSMONO_ICALL("MonoCore.Core::mq_GetAvilableDiscIds", mono_GetAvilableDiscIds);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpellIds", mono_GetSpellIds);

    // HSMONO_ICALL("MonoCore.Core::mq_GetRunNextCommand", mono_GetRunNextCommand);
    // HSMONO_ICALL("MonoCore.Core::mq_GetFocusedWindowName", mono_GetFocusedWindowName);
    // HSMONO_ICALL("MonoCore.Core::mono_GetHoverWindowName", mono_GetHoverWindowName);
    // HSMONO_ICALL("MonoCore.Core::mq_GetMQ2MonoVersion", mono_GetMQ2MonoVersion);

    // HSMONO_ICALL("MonoCore.Core::mq_ExecuteCommandByName", mono_ExecuteCommandByName);
    // HSMONO_ICALL("MonoCore.Core::mq_ExecuteCommand", mono_ExecuteCommand);
    // HSMONO_ICALL("MonoCore.Core::mq_LookAt", mono_LookAt);
    // HSMONO_ICALL("MonoCore.Core::mq_Memory_GetPageFileSize", mono_Memory_GetPageFileSize);

    //ImGui stuff
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker4", mono_ImGUI_ColorPicker4);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_GetRGBA", mono_ImGUI_ColorPicker_GetRGBA);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker4_Float", mono_ImGUI_ColorPicker4_Float);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_GetRGBA_Float", mono_ImGUI_ColorPicker_GetRGBA_Float);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleColorVec4", mono_ImGUI_GetStyleColorVec4);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_Clear", mono_ImGUI_ColorPicker_Clear);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin", mono_ImGUI_Begin);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Button", mono_ImGUI_Button);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ButtonEx", mono_ImGUI_ButtonEx);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InvisibleButton", mono_ImGUI_InvisibleButton);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SmallButton", mono_ImGUI_SmallButton);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_End", mono_ImGUI_End);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin_OpenFlagSet", mono_ImGUI_Begin_OpenFlagSet);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin_OpenFlagGet", mono_ImGUI_Begin_OpenFlagGet);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Text", mono_ImGUI_Text);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Separator", mono_ImGUI_Separator);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SameLine", mono_ImGUI_SameLine);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SameLineEx", mono_ImGUI_SameLineEx);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox", mono_ImGUI_Checkbox);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox_Clear", mono_ImGUI_Checkbox_Clear);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox_Get", mono_ImGUI_Checkbox_Get);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTabBar", mono_ImGUI_BeginTabBar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTabBar", mono_ImGUI_EndTabBar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTabItem", mono_ImGUI_BeginTabItem);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTabItem", mono_ImGUI_EndTabItem);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushID", mono_ImGUI_PushID);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopID", mono_ImGUI_PopID);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushItemWidth", mono_ImGUI_PushItemWidth);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopItemWidth", mono_ImGUI_PopItemWidth);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginChild", mono_ImGUI_BeginChild);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndChild", mono_ImGUI_EndChild);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Selectable", mono_ImGUI_Selectable);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Selectable_WithFlags", mono_ImGUI_Selectable_WithFlags);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetContentRegionAvailX", mono_ImGUI_GetContentRegionAvailX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetContentRegionAvailY", mono_ImGUI_GetContentRegionAvailY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText", mono_ImGUI_InputText);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText_Clear", mono_ImGUI_InputText_Clear);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CalcTextSizeX", mono_ImGUI_CalcTextSizeX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CalcTextSize", mono_ImGUI_CalcTextSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMinX", mono_ImGUI_GetWindowContentRegionMin_X);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMinY", mono_ImGUI_GetWindowContentRegionMin_Y);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMaxX", mono_ImGUI_GetWindowContentRegionMax_X);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMaxY", mono_ImGUI_GetWindowContentRegionMax_Y);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowPos", mono_ImGUI_SetNextWindowPos);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectSize", mono_ImGUI_GetItemRectSize);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowPosX", mono_ImGUI_GetWindowPosX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowPosY", mono_ImGUI_GetWindowPosY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowSizeX", mono_ImGUI_GetWindowSizeX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowSizeY", mono_ImGUI_GetWindowSizeY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowFocus", mono_ImGUI_SetNextWindowFocus);
    //mono_ImGUI_InputTextClear
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputTextMultiline", mono_ImGUI_InputTextMultiline);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText_Get", mono_ImGUI_InputText_Get);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextItemWidth", mono_ImGUI_SetNextItemWidth);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowHeight", mono_ImGUI_GetWindowHeight);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowWidth", mono_ImGUI_GetWindowWidth);

    // Combo wrappers
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginCombo", mono_ImGUI_BeginCombo);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndCombo", mono_ImGUI_EndCombo);
    // Right align helper
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_RightAlignButton", mono_ImGUI_RightAlignButton);

    // Context menus / popups
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginPopupContextItem", mono_ImGUI_BeginPopupContextItem);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginPopupContextWindow", mono_ImGUI_BeginPopupContextWindow);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndPopup", mono_ImGUI_EndPopup);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_MenuItem", mono_ImGUI_MenuItem);

    //Progress bar
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ProgressBar", mono_ImGUI_ProgressBar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ProgressBarGradient", mono_ImGUI_ProgressBarGradient);

    // Tables
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTable", mono_ImGUI_BeginTable);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTableS", mono_ImGUI_BeginTableSimple);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTable", mono_ImGUI_EndTable);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetupColumn", mono_ImGUI_TableSetupColumn);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetupColumn_Default", mono_ImGUI_TableSetupColumn_Default);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetBgColor", mono_ImGUI_TableSetBgColor);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetColumnIndex", mono_ImGUI_TableSetColumnIndex);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableHeadersRow", mono_ImGUI_TableHeadersRow);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableHeader", mono_ImGUI_TableHeader);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableGetColumnName", mono_ImGUI_TableGetColumnName);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextRow", mono_ImGUI_TableNextRow);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextColumn", mono_ImGUI_TableNextColumn);

    // Colors / styled text
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextColored", mono_ImGUI_TextColored);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleColor", mono_ImGUI_PushStyleColor);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopStyleColor", mono_ImGUI_PopStyleColor);

    // Style variables
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleVarFloat", mono_ImGUI_PushStyleVarFloat);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleVarVec2", mono_ImGUI_PushStyleVarVec2);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopStyleVar", mono_ImGUI_PopStyleVar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleVarFloat", mono_ImGUI_GetStyleVarFloat);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleVarVec2", mono_ImGUI_GetStyleVarVec2);

    // Text wrapping and window sizing
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextWrapped", mono_ImGUI_TextWrapped);
    // Expose unformatted text render to avoid printf-style format crashes
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextUnformatted", mono_ImGUI_TextUnformatted);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushTextWrapPos", mono_ImGUI_PushTextWrapPos);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopTextWrapPos", mono_ImGUI_PopTextWrapPos);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSizeConstraints", mono_ImGUI_SetNextWindowSizeConstraints);

    // New window control functions
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowBgAlpha", mono_ImGUI_SetNextWindowBgAlpha);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSize", mono_ImGUI_SetNextWindowSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSizeWithCond", mono_ImGUI_SetNextWindowSizeWithCond);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsWindowHovered", mono_ImGUI_IsWindowHovered);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowWidth", mono_ImGUI_GetWindowWidth);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowHeight", mono_ImGUI_GetWindowHeight);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsMouseClicked", mono_ImGUI_IsMouseClicked);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsMouseDoubleClicked", mono_ImGUI_IsMouseDoubleClicked);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsItemClicked", mono_ImGUI_IsItemClicked);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetClipboardText", mono_ImGUI_SetClipboardText);

    // Input int
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt", mono_ImGUI_InputInt);
    //mono_ImGUI_InputTextClear
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt_Get", mono_ImGUI_InputInt_Get);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt_Clear", mono_ImGUI_InputInt_Clear);
    //bool mono_ImGUI_InputIntClear(MonoString* id)

    //mono_ImGUI_InputInt

    // Sliders
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SliderInt", mono_ImGUI_SliderInt);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SliderDouble", mono_ImGUI_SliderDouble);

    // Tree nodes
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreeNode", mono_ImGUI_TreeNode);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreeNodeEx", mono_ImGUI_TreeNodeEx);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreePop", mono_ImGUI_TreePop);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CollapsingHeader", mono_ImGUI_CollapsingHeader);

    // Tooltips and hover detection
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsItemHovered", mono_ImGUI_IsItemHovered);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTooltip", mono_ImGUI_BeginTooltip);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTooltip", mono_ImGUI_EndTooltip);

    // Image display
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Image", mono_ImGUI_Image);

    // Fonts
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_AddFontFromFileTTF", mono_ImGUI_AddFontFromFileTTF);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushFont", mono_ImGUI_PushFont);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopFont", mono_ImGUI_PopFont);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushEQFont", mono_ImGUI_PushEQFont);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Style_GetFontSizeBase", mono_ImGUI_Style_GetFontSizeBase);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushMaterialIconsFont", mono_ImGUI_PushMaterialIconsFont);

    // Spell icon drawing
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_DrawSpellIconByIconIndex", mono_ImGUI_DrawSpellIconByIconIndex);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_DrawSpellIconBySpellID", mono_ImGUI_DrawSpellIconBySpellID);

    // Drawing functions for custom backgrounds
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextRowEx", mono_ImGUI_TableNextRowEx);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorPosX", mono_ImGUI_GetCursorPosX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetCursorPosX", mono_ImGUI_SetCursorPosX);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorPosY", mono_ImGUI_GetCursorPosY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetCursorPosY", mono_ImGUI_SetCursorPosY);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorScreenPosX", mono_ImGUI_GetCursorScreenPosX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorScreenPosY", mono_ImGUI_GetCursorScreenPosY);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetTextLineHeightWithSpacing", mono_ImGUI_GetTextLineHeightWithSpacing);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetTextLineHeight", mono_ImGUI_GetTextLineHeight);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetFrameHeight", mono_ImGUI_GetFrameHeight);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRectFilled", mono_ImGUI_GetWindowDrawList_AddRectFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRect", mono_ImGUI_GetWindowDrawList_AddRect);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRectFilledMultiColor", mono_ImGUI_GetWindowDrawList_AddRectFilledMultiColor);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddQuad", mono_ImGUI_GetWindowDrawList_AddQuad);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddQuadFilled", mono_ImGUI_GetWindowDrawList_AddQuadFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddTriangle", mono_ImGUI_GetWindowDrawList_AddTriangle);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddTriangleFilled", mono_ImGUI_GetWindowDrawList_AddTriangleFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddCircle", mono_ImGUI_GetWindowDrawList_AddCircle);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddCircleFilled", mono_ImGUI_GetWindowDrawList_AddCircleFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddNgon", mono_ImGUI_GetWindowDrawList_AddNgon);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddNgonFilled", mono_ImGUI_GetWindowDrawList_AddNgonFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddEllipse", mono_ImGUI_GetWindowDrawList_AddEllipse);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddEllipseFilled", mono_ImGUI_GetWindowDrawList_AddEllipseFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddBezierCubic", mono_ImGUI_GetWindowDrawList_AddBezierCubic);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddBezierQuadratic", mono_ImGUI_GetWindowDrawList_AddBezierQuadratic);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_ItemSize", mono_ImGUI_Internal_ItemSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_ItemAdd", mono_ImGUI_Internal_ItemAdd);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_CalcItemSize", mono_ImGUI_Internal_CalcItemSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddLine", mono_ImGUI_GetWindowDrawList_AddLine);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddText", mono_ImGUI_GetWindowDrawList_AddText);

    // Item rect + color helpers
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMinX", mono_ImGUI_GetItemRectMinX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMinY", mono_ImGUI_GetItemRectMinY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMaxX", mono_ImGUI_GetItemRectMaxX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMaxY", mono_ImGUI_GetItemRectMaxY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMin", mono_ImGUI_GetItemRectMin);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetColorU32", mono_ImGUI_GetColorU32);

    // Texture creation from raw data
    // HSMONO_ICALL("MonoCore.E3ImGUI::mq_CreateTextureFromData", mono_CreateTextureFromData);
    // HSMONO_ICALL("MonoCore.E3ImGUI::mq_DestroyTexture", mono_DestroyTexture);
}

#undef HSMONO_ICALL

HAILSTONE_EXPORT void InitializePlugin(void) {
    /*AddCommand("/hello", hello, 0);
    AddCommand("/webexample", webexample, 0);
    AddTopLevelObject("Example", example_tlo, 0);*/
    if (!hs::AddCommand("/mono", MonoCommand)) WriteChatf("[HSMono] /mono could not be added");
    WriteChatf("[HSMono] loaded");
}

HAILSTONE_EXPORT void ShutdownPlugin(void) {
    //RemoveCommand("/hello");
    //RemoveCommand("/webexample");
    //if (page) host->web_close(host->host, page); /* the client closes it anyway as the plugin unloads */
    // Every script stops before this plugin's code goes. Mono itself stays up (it can't be restarted in a process);
    // loading the plugin again joins it (InitMono).
    while (g_rootDomain && !monoAppDomains.empty()) UnloadScript(monoAppDomains.begin());
    hs::RemoveAllCommands();
    RemoveTopLevelObject("Example");
}

HAILSTONE_EXPORT void OnPulse(void) {

    if (!g_rootDomain) return;   // Mono isn't running: no /mono yet
    previousCommand = false;
    if (appDomainProcessQueue.size() < 1) return;
    std::chrono::steady_clock::time_point proccessingTimer = std::chrono::steady_clock::now(); //the time this was issued + m_delayTime
   
    HailstonePlayer p;
    p.size = sizeof p; //this is kinda like a version check to make sure we have the right size struct.
    if (!host->get_player(host->host, &p)) return;//no player or connection
    if (p.game_state != GAMESTATE_INGAME) return;


    
    char message[512];
    ++pulses;
    while (page && host->web_message(host->host, page, message, sizeof message) >= 0)
        WriteChatf("[HSMono] page: %s", message);
}

HAILSTONE_EXPORT void OnZoned(void) {
    ++zones;
    WriteChatf("[HSMono] zoned");
}

HAILSTONE_EXPORT int OnIncomingChat(const char* line, uint32_t color) {
    (void)line;
    (void)color;
    ++chats;
    return 0;
}

/* Hides lines with "spam" in them. */
HAILSTONE_EXPORT int OnFilterChat(const char* line, uint32_t color) {
    (void)color;
    return strstr(line, "spam") != 0;
}

/* Drops incoming 0x7e57 packets; rewrites outgoing 0x7e58 to four bytes 1 2 3 4. */
HAILSTONE_EXPORT int OnPacket(int stream, int direction, HailstonePacket* p) {
    (void)stream;
    if (direction == HAILSTONE_IN && p->opcode == 0x7e57) return 1;
    if (direction == HAILSTONE_OUT && p->opcode == 0x7e58 && host->resize_packet(host->host, p, 4)) {
        static const uint8_t bytes[4] = {1, 2, 3, 4};
        memcpy(p->data, bytes, 4);
    }
    return 0;
}
