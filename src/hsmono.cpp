/* HSMono: MQ2Mono for Hailstone. Runs C# scripts (E3 and the like) through an embedded Mono runtime, each in an app
 * domain of its own, with MQ2Mono's /mono command and its MonoCore.Core / MonoCore.E3ImGUI internal calls.
 * This file: the plugin's hooks, /mono, and the MonoCore.Core internal calls (hsmono.hpp lists the other sources).
 * Built against the SDK alone (sdk/hailstone_plugin.h, hailstone_plugin.hpp); a server with a hailstone.cfg may keep
 * the player's own plugins off (docs/plugins.md). */

#include "hsmono.hpp"

#include <atomic>
#include <mutex>
#include <thread>

HAILSTONE_PLUGIN

static uint64_t pulses, chats, zones;
std::atomic<bool> previousCommand{false}; //a command was issued on this call.
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

// ---- the HSMono TLO, MQ2Mono's ${MQ2Mono} under this plugin's name:
//   ${HSMono}                           TRUE
//   ${HSMono.Query[e3,E3Bots(Necro01).Query(ShowClass)]}
//                                       the text after the first comma, trimmed, goes to the e3 script's OnQuery, and
//                                       its string comes back; empty with no comma, no such script or no answer.
// [] can't appear inside the brackets (they end the index), so a query uses () as above.
// Unlike MQ2Mono: the script's name matches in any case (as /mono's does), and an exception in OnQuery is caught.

std::string queryAnswer;   // the client copies a TLO's string when the TLO returns

bool HSMonoTlo(const char*, const char* member, const char* member_index, HailstoneValue& out) {
    if (!member) {
        out = hs::Bool(true);
        return true;
    }
    if (!ci_equals(member, "Query")) return false;
    std::string answer;
    const std::string_view arg = member_index ? member_index : "";
    if (const size_t comma = arg.find(','); comma != std::string_view::npos)
        answer = QueryScript(Trim(arg.substr(0, comma)), std::string(Trim(arg.substr(comma + 1))));
    queryAnswer = std::move(answer);   // after the call: OnQuery may itself have read ${HSMono.Query[...]}
    out = hs::String(queryAnswer.c_str());
    return true;
}

void RunPendingMonoCommands() {
    while (!pendingMonoCommands.empty()) {
        const std::string line = std::move(pendingMonoCommands.front());
        pendingMonoCommands.pop_front();
        MonoCommand(line.c_str());
    }
}

} // namespace

// ---- the MonoCore.Core internal calls (MQ2Mono.cpp's mono_*)

namespace {

// The client's thread. Hailstone answers host calls on it, and on a script's own thread while Invoke lends it the
// client's (E3's MainProcessor runs on one while the client's thread waits in OnPulse; docs/plugins.md "Threads").
std::thread::id clientThread;
std::atomic<bool> offThreadCall{false};   // reported in chat at the next pulse (WriteChatf is a host call too)

bool HostAnswersHere() { return std::this_thread::get_id() == clientThread || ClientThreadLent(); }

bool MayCallHost() {
    if (HostAnswersHere()) return true;
    offThreadCall.store(true, std::memory_order_relaxed);
    return false;
}

std::string Utf8(MonoString* s) {
    if (!s) return {};
    char* c = mono.mono_string_to_utf8(s);
    std::string out = c ? c : "";
    if (c) mono.mono_free(c);
    return out;
}

// A string for the C# side, in the calling script's domain (MQ2Mono's mono_string_new_wrapper).
MonoString* NewString(const char* s) {
    MonoDomain* d = mono.mono_domain_get();
    return mono.mono_string_new(d ? d : g_rootDomain, s ? s : "");
}

// mq_ParseTLO: `text` with each ${...} replaced (ParseMacroData). "NULL" where MQ2Mono's ParseMacroData failed: here
// without the 'read' capability, and from a thread Hailstone answers nothing on (MayCallHost).
// MQ2Mono parsed into MAX_STRING (2048) bytes; this takes up to 16 KiB and cuts a longer answer there, since parsing
// again with more room would run each TLO twice. Hailstone has one parser, so MQ2Mono's gParserVersion 2 has no
// counterpart.
MonoString* mono_ParseTLO(MonoString* text) {
    if (!MayCallHost()) return NewString("NULL");
    const std::string in = Utf8(text);
    char out[16384];
    out[0] = '\0';
    if (ParseMacroData(in.c_str(), out, sizeof out) < 0) return NewString("NULL");
    return NewString(out);
}

// Lines a script asked to run later (mq_DoCommandDelayed, or mq_DoCommand from a thread Hailstone answers nothing on):
// run at the start of the next pulse, on the client's thread, in order. Locked: E3 has threads besides its
// MainProcessor.
std::mutex delayedCommandsLock;
std::deque<std::string> delayedCommands;

void QueueCommand(std::string line) {
    std::lock_guard<std::mutex> lock(delayedCommandsLock);
    delayedCommands.push_back(std::move(line));
}

void RunDelayedCommands() {
    std::deque<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(delayedCommandsLock);
        lines.swap(delayedCommands);
    }
    for (const std::string& line : lines) DoCommand(line.c_str());   // a line may queue more: they wait a pulse
}

// mq_DoCommand: runs the line now, as typed, with this plugin's capabilities (MQ2Mono's HideDoCommand, not delayed).
// From a thread Hailstone answers nothing on, it waits for the next pulse instead, where MQ2Mono ran it at once.
// Either way OnPulse gives the other scripts no turn after it this pulse (previousCommand).
void mono_DoCommand(MonoString* text) {
    std::string line = Utf8(text);
    previousCommand = true;
    if (HostAnswersHere()) DoCommand(line.c_str());
    else QueueCommand(std::move(line));
}

// mq_DoCommandDelayed: the line runs at the start of the next pulse (MQ2Mono's HideDoCommand with bDelayed, which MQ
// runs from its command queue on a later pulse).
void mono_DoCommandDelayed(MonoString* text) {
    previousCommand = true;
    QueueCommand(Utf8(text));
}

} // namespace

// mono_runtime.h. The namespace and classes are fixed by the C# side: MonoCore.Core here, MonoCore.E3ImGUI in
// hsmono_imgui.cpp. Each line stays commented out until its mono_* function is in this file (above this point, or
// declared there).
void RegisterInternalCalls() {
    // HSMONO_ICALL("MonoCore.Core::mq_Echo", mono_Echo);
    HSMONO_ICALL("MonoCore.Core::mq_ParseTLO", mono_ParseTLO);
    HSMONO_ICALL("MonoCore.Core::mq_DoCommand", mono_DoCommand);
    HSMONO_ICALL("MonoCore.Core::mq_DoCommandDelayed", mono_DoCommandDelayed);
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
    clientThread = std::this_thread::get_id();   // the client calls every hook on its thread
    if (!hs::AddCommand("/mono", MonoCommand)) WriteChatf("[HSMono] /mono could not be added");
    if (!hs::AddTopLevelObject("HSMono", HSMonoTlo)) WriteChatf("[HSMono] the HSMono TLO could not be added");
    WriteChatf("[HSMono] loaded");
}

HAILSTONE_API void ShutdownPlugin(void) {
    // Every script stops before this plugin's code goes. Mono itself stays up, unlike MQ2Mono's mono_jit_cleanup: Mono
    // can't start again in the same process, and loading the plugin again joins the running one (InitMono).
    UnloadAllScripts();
    pendingMonoCommands.clear();
    {
        std::lock_guard<std::mutex> lock(delayedCommandsLock);
        delayedCommands.clear();
    }
    hs::RemoveAllCommands();
    hs::RemoveAllTopLevelObjects();
}

HAILSTONE_API void OnPulse(void) {
    if (!g_rootDomain) return;   // Mono isn't running: no /mono yet
    if (offThreadCall.exchange(false, std::memory_order_relaxed)) {
        static bool told = false;   // once: E3 asks from its thread all the time
        if (!told) {
            told = true;
            WriteChatf("[HSMono] a script called Hailstone from a thread of its own while no script call was running, "
                       "or on a client without lend_thread; those calls got nothing (NULL)");
        }
    }
    RunPendingMonoCommands();
    RunDelayedCommands();
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
