/* HSMono: MQ2Mono for Hailstone. Runs C# scripts (E3 and the like) through an embedded Mono runtime, each in an app
 * domain of its own, with MQ2Mono's /mono command and its MonoCore.Core / MonoCore.E3ImGUI internal calls.
 * This file: the plugin's hooks, /mono, and the MonoCore.Core internal calls (hsmono.hpp lists the other sources).
 * Built against the SDK alone (sdk/hailstone_plugin.h, hailstone_plugin.hpp); a server with a hailstone.cfg may keep
 * the player's own plugins off (docs/plugins.md). */

#include "hsmono.hpp"

HAILSTONE_PLUGIN

static uint64_t pulses, chats, zones;
bool previousCommand = false; //a command was issued on this call.
int _milisecondsToProcess = 8;

// ---- /mono, as MQ2Mono's: list, version, load|run|start <name>, unload|stop <name|all>, and /mono <name> to load.

namespace {

std::deque<std::string> pendingMonoCommands;   // /mono lines from inside a call into C#: they run at the next pulse

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

void MonoCommand(const char* args) {
    // A script may type /mono itself (restarting or stopping itself). Its domain may be the one running, so the line
    // waits for the next pulse, outside any C# code.
    if (InScriptCall()) {
        pendingMonoCommands.emplace_back(args ? args : "");
        return;
    }

    const std::string_view line = Trim(args ? args : "");
    const size_t split = line.find_first_of(" \t");
    const std::string_view first = line.substr(0, split);
    const std::string_view second = split == std::string_view::npos ? std::string_view() : Trim(line.substr(split));

    if (line.empty()) {
        WriteChatf("[HSMono] /mono list | version | load <name> | unload <name|all> | <name>");
        return;
    }
    if (ci_equals(first, "list")) {
        WriteChatf("[HSMono] List Running Processes.");
        for (const auto& [name, script] : monoAppDomains) WriteChatf("[HSMono] %s", script.m_appDomainName.c_str());
        WriteChatf("[HSMono] End List.");
        return;
    }
    if (ci_equals(first, "version")) {
        WriteChatf("[HSMono] Version: %s", kHsMonoVersion);
        return;
    }
    if (ci_equals(first, "unload") || ci_equals(first, "stop")) {
        if (second.empty()) {
            WriteChatf("[HSMono] What should I unload?");
        } else if (ci_equals(second, "all")) {
            UnloadAllScripts();
            WriteChatf("[HSMono] Finished Unloading all scripts.");
        } else {
            const std::string name(second);
            WriteChatf("[HSMono] Unloading %s", name.c_str());
            if (UnloadScript(name)) WriteChatf("[HSMono] Finished Unloading %s", name.c_str());
            else WriteChatf("[HSMono] Cannot find %s", name.c_str());
        }
        return;
    }

    std::string_view target = first;   // /mono <name>
    if (ci_equals(first, "load") || ci_equals(first, "run") || ci_equals(first, "start")) {
        if (second.empty()) {
            WriteChatf("[HSMono] What should I load?");
            return;
        }
        target = second;
    }
    const std::optional<std::string> name = ScriptName(target);
    if (!name) {
        WriteChatf("[HSMono] not a script name: '%.*s'", static_cast<int>(target.size()), target.data());
        return;
    }
    if (std::optional<std::string> error = InitMono()) {   // the first load starts the runtime
        ChatLines(*error);
        return;
    }
    WriteChatf("[HSMono] Loading %s", name->c_str());
    if (std::optional<std::string> error = LoadScript(*name)) {
        ChatLines(*error);
        return;
    }
    WriteChatf("[HSMono] Finished Loading %s", name->c_str());
}

void RunPendingMonoCommands() {
    while (!pendingMonoCommands.empty()) {
        const std::string line = std::move(pendingMonoCommands.front());
        pendingMonoCommands.pop_front();
        MonoCommand(line.c_str());
    }
}

} // namespace

// mono_runtime.h. The namespace and classes are fixed by the C# side: MonoCore.Core here, MonoCore.E3ImGUI in
// hsmono_imgui.cpp. Each line stays commented out until its mono_* function is in this file (above this point, or
// declared there).
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

    RegisterImGuiInternalCalls();
}

HAILSTONE_API void InitializePlugin(void) {
    if (!hs::AddCommand("/mono", MonoCommand)) WriteChatf("[HSMono] /mono could not be added");
    WriteChatf("[HSMono] loaded");
}

HAILSTONE_API void ShutdownPlugin(void) {
    // Every script stops before this plugin's code goes. Mono itself stays up, unlike MQ2Mono's mono_jit_cleanup: Mono
    // can't start again in the same process, and loading the plugin again joins the running one (InitMono).
    UnloadAllScripts();
    pendingMonoCommands.clear();
    hs::RemoveAllCommands();
}

HAILSTONE_API void OnPulse(void) {
    if (!g_rootDomain) return;   // Mono isn't running: no /mono yet
    RunPendingMonoCommands();
    previousCommand = false;
    if (appDomainProcessQueue.empty()) return;

    // Out of the game (camped to character select, disconnected): as MQ2Mono, every script stops.
    const std::optional<HailstonePlayer> player = hs::GetPlayer();
    if (!player || player->game_state != GAMESTATE_INGAME) {
        UnloadAllScripts();
        return;
    }

    // Each script's OnPulse in turn, round robin, for at most _milisecondsToProcess per pulse. A script that issued a
    // command ends the pulse, so the command runs before the next script gets its turn.
    const std::chrono::steady_clock::time_point processingTimer = std::chrono::steady_clock::now();
    size_t count = 0;
    const size_t queueSize = appDomainProcessQueue.size();
    while (count < queueSize) {
        std::string key = appDomainProcessQueue.front();
        appDomainProcessQueue.pop_front();
        appDomainProcessQueue.push_back(key);
        ++count;

        auto it = monoAppDomains.find(key);
        if (it == monoAppDomains.end()) {   // should never happen; drop the stale key
            appDomainProcessQueue.pop_back();
            continue;
        }
        monoAppDomainInfo& script = it->second;
        // mq_Delay: no OnPulse until its time is up
        if (script.m_delayTime > 0 && std::chrono::steady_clock::now() > script.m_delayTimer) script.m_delayTime = 0;
        if (script.m_delayTime == 0) Invoke(script, script.m_OnPulseMethod, "OnPulse");

        if (previousCommand) break;
        if (std::chrono::steady_clock::now() > processingTimer + std::chrono::milliseconds(_milisecondsToProcess) ||
            count >= appDomainProcessQueue.size())
            break;
    }
    ++pulses;
  }

// Each script's windows (MQ2Mono's OnUpdateImGui). Needs the 'ui' capability.
HAILSTONE_API void OnUpdateImGui(void) { RunScriptsImGui(); }

HAILSTONE_API void OnZoned(void) {
    ++zones;
    WriteChatf("[HSMono] zoned");
}

HAILSTONE_API int OnIncomingChat(const char* line, uint32_t color) {
    (void)line;
    (void)color;
    ++chats;
    return 0;
}

/* Hides lines with "spam" in them. */
HAILSTONE_API int OnFilterChat(const char* line, uint32_t color) {
    return 0;
    (void)color;
    return strstr(line, "spam") != 0;
}

/* Drops incoming 0x7e57 packets; rewrites outgoing 0x7e58 to four bytes 1 2 3 4. */
HAILSTONE_API int OnPacket(int stream, int direction, HailstonePacket* p) {
    /*(void)stream;
    if (direction == HAILSTONE_IN && p->opcode == 0x7e57) return 1;
    if (direction == HAILSTONE_OUT && p->opcode == 0x7e58 && host->resize_packet(host->host, p, 4)) {
        static const uint8_t bytes[4] = {1, 2, 3, 4};
        memcpy(p->data, bytes, 4);
    }
    */

    return 0;
}
