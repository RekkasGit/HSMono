/* HSMono: MQ2Mono for Hailstone. Runs C# scripts (E3 and the like) through an embedded Mono runtime, each in an app
 * domain of its own, with MQ2Mono's /mono command and its MonoCore.Core / MonoCore.E3ImGUI internal calls.
 * This file: the plugin's hooks, /mono, and the MonoCore.Core internal calls (hsmono.hpp lists the other sources).
 * Built against the SDK alone (sdk/hailstone_plugin.h, hailstone_plugin.hpp); a server with a hailstone.cfg may keep
 * the player's own plugins off (docs/plugins.md). */

#include "hsmono.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

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

// mono_loader.cpp: the OS calls.
std::uint64_t PrivateMemoryBytes();

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

// Lines a script asked to run later (mq_DoCommandDelayed, or mq_DoCommand and mq_Echo from a thread Hailstone answers
// nothing on): run at the start of the next pulse, on the client's thread, in order. Locked: E3 has threads besides its
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

// mq_Echo: a line in the chat window, as MQ2Mono's WriteChatf, written whole (without WriteChatf's 2 KiB cut).
// MacroQuest's colour codes in it ("\ag", "\a-w", "\a#336699", "\ax": E3 colours most of its lines) are the client's
// to draw (docs/plugins.md "Colour codes"). From a thread Hailstone answers nothing on it is queued as an /echo of the
// line for the next pulse, in order with the delayed commands.
void mono_Echo(MonoString* text) {
    std::string line = Utf8(text);
    if (HostAnswersHere()) host->write_chat(host->host, line.c_str(), 0);
    else QueueCommand("/echo " + line);
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

// mq_AddCommand, mq_RemoveCommand, mq_ClearCommands: the calling script's own commands (its domain's,
// monoAppDomainInfo's AddScriptCommand / RemoveCommand / ClearCommands in app_domains.cpp). A command runs the script's
// OnCommand with the whole line, and goes when the script unloads. From the root domain they do nothing (MQ2Mono
// would make an empty script entry there), and from a thread Hailstone answers nothing on they do nothing either.

bool mono_AddCommand(MonoString* text) {
    monoAppDomainInfo* script = CurrentScript();
    if (!script || !MayCallHost()) return false;
    return script->AddScriptCommand(Utf8(text));   // false when the name is taken (MQ2Mono's AddCommand)
}

void mono_RemoveCommand(MonoString* text) {
    monoAppDomainInfo* script = CurrentScript();
    if (script && MayCallHost()) script->RemoveCommand(Utf8(text));
}

void mono_ClearCommands() {
    monoAppDomainInfo* script = CurrentScript();
    if (script && MayCallHost()) script->ClearCommands();
}

// mq_GetXtargetInfo: one record per extended target slot, slot 0 first: i32 type (XTargetType), i32 spawn id, i32 aggro
// percent, i32 HP percent. MQ2Mono wrote those 16 bytes per slot itself; the client's get_xtarget_data_bulk writes the
// same record (HS_BulkXTargetData), so it fills the buffer directly. The slots are every one the server sent (RoF2:
// at most 20), where MQ2Mono walked the client's list.
// The buffer stays the scripts' to read after the call: E3 keeps the pointer and reads it again for 250 ms. So it is
// this plugin's own, not the bulk buffer (which goes back when the callback ends). Rewritten at each call, as
// MQ2Mono's static one was.
static_assert(sizeof(HS_BulkXTargetData) == 16 && HS_BULK_XTARGET_RECORD_SIZE == 16,
              "mq_GetXtargetInfo's record is MQ2Mono's 16 bytes");
unsigned char xtargetInfo[1024];   // 64 slots

unsigned char* mono_GetXtargetInfo(int32_t* bufferLength) {
    int32_t written = 0;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_xtarget_data_bulk))
        written = host->get_xtarget_data_bulk(host->host, xtargetInfo, static_cast<int32_t>(sizeof xtargetInfo));
    if (bufferLength) *bufferLength = written > 0 ? written : 0;   // 0: no slots, or without 'read'
    return xtargetInfo;
}

// The ids of an id-and-name list (get_avail_aa_data_bulk, get_avail_disc_data_bulk) as MQ2Mono's flat list of i32
// ids, written to `out`; the bytes written. The names are dropped: MQ2Mono's lists have none. A list longer than the
// first read's buffer (the header's total past its count) is read once more into a buffer big enough.
using BulkRead = int32_t (*)(void* host, unsigned char* out, int32_t out_size);

int32_t IdsOf(BulkRead read, unsigned char* out, size_t outSize) {
    std::vector<unsigned char> list(16384);   // RoF2's lists have at most 300 entries each
    HS_BulkListHeader header{};
    int32_t filled = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        filled = read(host->host, list.data(), static_cast<int32_t>(list.size()));
        if (filled < HS_BULK_LIST_HEADER_SIZE) return 0;   // without 'read', or no room for the header
        std::memcpy(&header, list.data(), sizeof header);
        if (header.count >= header.total) break;
        list.resize(list.size() / static_cast<size_t>(header.count > 0 ? header.count : 1) *
                        static_cast<size_t>(header.total) + 4096);
    }
    size_t at = HS_BULK_LIST_HEADER_SIZE, written = 0;
    for (int32_t i = 0; i < header.count && written + sizeof(int32_t) <= outSize; ++i) {
        if (at + HAILSTONE_ID_NAME_RECORD_SIZE > static_cast<size_t>(filled)) break;
        HAILSTONE_ID_NAME_RECORD record;
        std::memcpy(&record, list.data() + at, sizeof record);
        at += HAILSTONE_ID_NAME_RECORD_SIZE + static_cast<size_t>(record.nameSize > 0 ? record.nameSize : 0);
        std::memcpy(out + written, &record.id, sizeof record.id);
        written += sizeof record.id;
    }
    return static_cast<int32_t>(written);
}

// As mq_GetXtargetInfo's, these buffers stay the scripts' to read after the call, and each call rewrites its own.
unsigned char availableAAIds[16384];
unsigned char availableDiscIds[16384];

// mq_GetAvailableAAIds: the alternate abilities the character owns, as i32 ids (get_avail_aa_data_bulk, in the owned
// list's order). Each id is the ability's group id, which every rank shares and the client's AA reads take.
// NOTE: MQ2Mono wrote CAltAbilityData::ID, from a server's AscendantGetOwnedAAs export or else the client's owned
// list; whether that is the group id or a rank id, and so whether E3's ${Me.AltAbility[id]} lookups match these, is
// not confirmed. Hailstone has no Me.AltAbility TLO yet either (docs/scripting-reference.md).
unsigned char* mono_GetAvailableAAIds(int32_t* bufferLength) {
    int32_t written = 0;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_avail_aa_data_bulk))
        written = IdsOf(host->get_avail_aa_data_bulk, availableAAIds, sizeof availableAAIds);
    if (bufferLength) *bufferLength = written;
    return availableAAIds;
}

// mq_GetAvilableDiscIds (MQ2Mono's spelling): the character's combat abilities, as i32 spell ids
// (get_avail_disc_data_bulk: the profile's discipline list without its empty entries, in list order). MQ2Mono listed
// the ones the Combat Skills window shows (ShouldDisplayThisSkill); this lists every one the profile holds.
unsigned char* mono_GetAvilableDiscIds(int32_t* bufferLength) {
    int32_t written = 0;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_avail_disc_data_bulk))
        written = IdsOf(host->get_avail_disc_data_bulk, availableDiscIds, sizeof availableDiscIds);
    if (bufferLength) *bufferLength = written;
    return availableDiscIds;
}

// mq_GetHSMonoVersion: this plugin's version, kHsMonoVersion. HSMono's own name for it; new scripts call this one.
// Asks nothing of the client, so any thread may call it.
MonoString* mono_GetHSMonoVersion() { return NewString(kHsMonoVersion); }

// mq_GetMQ2MonoVersion: legacy, for scripts written for MQ2Mono (E3 among them, which parses it as a decimal in
// en-US). The same answer as mq_GetHSMonoVersion.
MonoString* mono_GetMQ2MonoVersion() { return NewString(kHsMonoVersion); }

// ---- spells: get_spells_data_bulk and get_spells_by_name_data_bulk (docs/plugins.md "get_spells_data_bulk")

// MQ's IsNumber, as MQ2Mono's spell calls use it: an id when the query is all digits, else a name.
bool IsSpellId(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// One spell's bulk record, from its fixed part to its last effect line, for MQ2Mono's GetSpellByID or GetSpellByName:
// by name, the first spell of that name in get_spells_by_name_data_bulk's order (lowest id first). Empty when the
// client holds no such spell or can't be asked (no 'read', older client).
// NOTE: which of several spells of one name MQ's GetSpellByName picks is not confirmed; this takes the lowest id.
// The record is in a buffer of this file's, good until the next call (E3 asks for one spell 13 times in a row, so the
// buffer is made once, not each time).
std::span<const unsigned char> SpellRecord(const std::string& query) {
    static std::vector<unsigned char> out(64 * 1024);   // a spell's ranks by name: a few hundred bytes to ~2 KiB each
    int32_t n = 0;
    if (IsSpellId(query)) {
        if (!HAILSTONE_HOST_HAS(host, get_spells_data_bulk)) return {};
        const long id = std::strtol(query.c_str(), nullptr, 10);
        const int32_t ids[1] = {id > 0 && id <= INT32_MAX ? static_cast<int32_t>(id) : 0};
        n = host->get_spells_data_bulk(host->host, ids, 1, out.data(), static_cast<int32_t>(out.size()));
    } else {
        if (!HAILSTONE_HOST_HAS(host, get_spells_by_name_data_bulk)) return {};
        n = host->get_spells_by_name_data_bulk(host->host, query.c_str(), out.data(), static_cast<int32_t>(out.size()));
    }
    int32_t count = 0;
    HS_BulkSpellFixedRecord record{};
    if (n < HS_BULK_SPELL_HEADER_SIZE + HS_BULK_SPELL_RECORD_SIZE) return {};
    std::memcpy(&count, out.data(), sizeof count);
    std::memcpy(&record, out.data() + HS_BULK_SPELL_HEADER_SIZE, sizeof record);
    if (count < 1 || !record.found || record.recordSize < HS_BULK_SPELL_RECORD_SIZE ||
        record.recordSize > n - HS_BULK_SPELL_HEADER_SIZE)
        return {};
    return std::span<const unsigned char>(out.data() + HS_BULK_SPELL_HEADER_SIZE,
                                          static_cast<size_t>(record.recordSize));
}

// A spell's effect slots and the client's text for those that have one (MQ's ParseSpellEffect: "Slot <n>: ...").
struct SpellEffects {
    int32_t slotCount = 0;
    std::vector<std::string> lines;   // in slot order; a slot with nothing to show has none
};

std::optional<SpellEffects> EffectsOf(const std::string& query) {
    const std::span<const unsigned char> rec = SpellRecord(query);
    if (rec.empty()) return std::nullopt;
    HS_BulkSpellFixedRecord fixed;
    std::memcpy(&fixed, rec.data(), sizeof fixed);
    SpellEffects out;
    out.slotCount = fixed.slotCount;
    // Past the slots and the class levels, then stringCount strings, then effectCount lines: each a u16 length and
    // its bytes.
    size_t at = HS_BULK_SPELL_RECORD_SIZE + static_cast<size_t>(fixed.slotCount) * static_cast<size_t>(fixed.slotSize) +
                static_cast<size_t>(fixed.classCount);
    for (int32_t i = 0; i < fixed.stringCount + fixed.effectCount; ++i) {
        uint16_t len = 0;
        if (at + sizeof len > rec.size()) break;
        std::memcpy(&len, rec.data() + at, sizeof len);
        at += sizeof len;
        if (at + len > rec.size()) break;
        if (i >= fixed.stringCount) out.lines.emplace_back(reinterpret_cast<const char*>(rec.data() + at), len);
        at += len;
    }
    return out;
}

// mq_GetSpellDataEffectCount: the spell's effect slots (MQ2Mono's GetSpellNumEffects), -1 for no such spell. An id or
// a name. 12 for a stock RoF2 spell, blank slots included.
// NOTE: GetSpellNumEffects is taken as the spell's slot count; if MQ counts only the slots in use, E3 asks here for a
// few more lines than it would there, each of them "".
int32_t mono_GetSpellDataEffectCount(MonoString* query) {
    if (!MayCallHost()) return -1;
    const std::optional<SpellEffects> effects = EffectsOf(Utf8(query));
    return effects ? effects->slotCount : -1;
}

// mq_GetSpellDataEffect: slot `line`'s text (0 is slot 1), MQ's ParseSpellEffect as MQ2Mono gives it: "" for a slot
// with nothing to show, "NULL" for no such spell or a line out of range. Each call reads the whole spell again (as
// MQ2Mono's looked it up again): mq_GetSpellsData gets every slot's text at once.
MonoString* mono_GetSpellDataEffect(MonoString* query, int32_t line) {
    if (!MayCallHost()) return NewString("NULL");
    const std::optional<SpellEffects> effects = EffectsOf(Utf8(query));
    if (!effects || line < 0 || line >= effects->slotCount) return NewString("NULL");
    const std::string prefix = "Slot " + std::to_string(line + 1) + ": ";
    for (const std::string& text : effects->lines)
        if (text.compare(0, prefix.size(), prefix) == 0) return NewString(text.c_str());
    return NewString("");
}

// HSMono's own, for scripts moving off the two above: get_spells_data_bulk's records as they are, so a script reads
// every field and every effect line of many spells in one call (docs/plugins.md "get_spells_data_bulk" has the
// layout: an i32 count, then one record per spell, each stepped by its recordSize). The buffer is this plugin's and
// stays the script's to read until its next call of the same function.
// Made at load at a size that holds what scripts ask for (a spell record is a few hundred bytes to ~2 KiB): 1 MiB is
// some 500 spells by id, 64 KiB every rank of a name. A read that needs more grows its buffer, which then keeps that
// size: neither is ever made smaller, so a script's calls don't allocate.
std::vector<unsigned char> spellsData(1024 * 1024);
std::vector<unsigned char> spellsByNameData(64 * 1024);

// Grows `buf` (never shrinks it) to at least `first`, then on until the read leaves room for one more record: the
// reads give whole records only.
template <typename Read>
int32_t ReadSpells(std::vector<unsigned char>& buf, size_t first, int32_t wanted, Read read) {
    if (buf.size() < first) buf.resize(first);
    int32_t n = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        n = read(buf.data(), static_cast<int32_t>(buf.size()));
        int32_t count = 0;
        if (n >= HS_BULK_SPELL_HEADER_SIZE) std::memcpy(&count, buf.data(), sizeof count);
        const bool all = wanted >= 0 ? count >= wanted : static_cast<size_t>(n) + 8192 <= buf.size();
        if (n < HS_BULK_SPELL_HEADER_SIZE || all) break;
        buf.resize(buf.size() * 2);
    }
    return n > 0 ? n : 0;
}

// mq_GetSpellsData(int[] ids, out int length): a record per id, in order (found 0 for an id the client has no spell
// for).
unsigned char* mono_GetSpellsData(MonoArray* ids, int32_t* length) {
    int32_t n = 0;
    const size_t count = ids ? mono.mono_array_length(ids) : 0;
    if (count > 0 && count <= 100000 && MayCallHost() && HAILSTONE_HOST_HAS(host, get_spells_data_bulk)) {
        const auto* first = reinterpret_cast<const int32_t*>(mono.mono_array_addr_with_size(ids, sizeof(int32_t), 0));
        const auto wanted = static_cast<int32_t>(count);
        n = ReadSpells(spellsData, HS_BULK_SPELL_HEADER_SIZE + count * 2048, wanted, [&](unsigned char* out, int32_t size) {
            return host->get_spells_data_bulk(host->host, first, wanted, out, size);
        });
    }
    if (n == 0) std::memset(spellsData.data(), 0, HS_BULK_SPELL_HEADER_SIZE);   // count 0
    if (length) *length = n;
    return spellsData.data();
}

// mq_GetSpellsDataByName(string name, out int length): every spell of that name (any case, the whole name), lowest id
// first.
unsigned char* mono_GetSpellsDataByName(MonoString* name, int32_t* length) {
    int32_t n = 0;
    const std::string wanted = Utf8(name);
    if (!wanted.empty() && MayCallHost() && HAILSTONE_HOST_HAS(host, get_spells_by_name_data_bulk)) {
        n = ReadSpells(spellsByNameData, 0, -1, [&](unsigned char* out, int32_t size) {
            return host->get_spells_by_name_data_bulk(host->host, wanted.c_str(), out, size);
        });
    }
    if (n == 0) std::memset(spellsByNameData.data(), 0, HS_BULK_SPELL_HEADER_SIZE);
    if (length) *length = n;
    return spellsByNameData.data();
}

// mq_GetSpellIds: the ids of every spell named `spellName` (any case, the whole name), as an int[] in the calling
// script's domain: get_spells_by_name_data_bulk's records, their ids alone. Read into a buffer of its own (made once,
// kept), not mq_GetSpellsDataByName's, which a script may still be reading.
// MQ2Mono's map compared names without case too; it gave them in no set order, this lowest id first. Empty for no such
// spell, without 'read', or from a thread Hailstone answers nothing on.
MonoArray* mono_GetSpellIds(MonoString* spellName) {
    static std::vector<unsigned char> records(64 * 1024);
    std::vector<int32_t> ids;
    const std::string wanted = Utf8(spellName);
    if (!wanted.empty() && MayCallHost() && HAILSTONE_HOST_HAS(host, get_spells_by_name_data_bulk)) {
        const int32_t n = ReadSpells(records, 0, -1, [&](unsigned char* out, int32_t size) {
            return host->get_spells_by_name_data_bulk(host->host, wanted.c_str(), out, size);
        });
        int32_t count = 0;
        if (n >= HS_BULK_SPELL_HEADER_SIZE) std::memcpy(&count, records.data(), sizeof count);
        size_t at = HS_BULK_SPELL_HEADER_SIZE;
        for (int32_t i = 0; i < count && at + HS_BULK_SPELL_RECORD_SIZE <= static_cast<size_t>(n); ++i) {
            HS_BulkSpellFixedRecord record;
            std::memcpy(&record, records.data() + at, sizeof record);
            if (record.recordSize < HS_BULK_SPELL_RECORD_SIZE) break;
            if (record.found) ids.push_back(record.id);
            at += static_cast<size_t>(record.recordSize);
        }
    }
    MonoDomain* d = mono.mono_domain_get();
    MonoArray* out = mono.mono_array_new(d ? d : g_rootDomain, mono.mono_get_int32_class(), ids.size());
    for (size_t i = 0; i < ids.size(); ++i)
        std::memcpy(mono.mono_array_addr_with_size(out, sizeof(int32_t), i), &ids[i], sizeof(int32_t));
    return out;
}

// mq_Delay: the calling script gets no OnPulse for `milliseconds` (OnPulse checks m_delayTimer, as MQ2Mono's does).
// E3 calls it from its OnPulse to hand its worker thread the time. Asks nothing of the client, so any thread may call
// it; from the root domain it does nothing (MQ2Mono would make an empty script entry there).
// A negative delay is taken as 0: MQ2Mono stored it, and a script with a negative m_delayTime never pulsed again.
void mono_Delay(int32_t milliseconds) {
    monoAppDomainInfo* script = CurrentScript();
    if (!script) return;
    if (milliseconds < 0) milliseconds = 0;
    script->m_delayTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    script->m_delayTime = milliseconds;
}

// mq_GetBuffData: the player's buff slots, then the song slots, MQ2Mono's 29 bytes each: i32 spell id, caster's spawn
// id, tics left, hits left, spell type, counter type, counter total, then u8 is-song. get_buff_data_bulk writes that
// record (HS_BulkBuffData), so its records are copied as they are: RoF2's 42 buff slots, then its 20 song slots.
// MQ2Mono wrote NUM_LONG_BUFFS records (62 in MQ's eqlib) and then NUM_SONG_BUFFS (30); E3 reads the first 42 as buffs
// and the next 20 as songs, by position (e3util.MaxBuffSlots, MaxSongSlots), which is where the client puts them.
// The client's last record, the active discipline's slot (slot type 2), is left out: MQ2Mono's list has no such slot,
// and a reader that walked to the end would take it for a song.
// The counter total is the counters left, as MQ2Mono's (the buff's slot data), once the client knows them: from the
// spell landing on the player, or a server that sends slot data. A buff from the profile or a zone-in list reads the
// counters the spell starts with until then (get_buff_data_bulk, docs/plugins.md).
static_assert(sizeof(HS_BulkBuffData) == 29 && HAILSTONE_BUFF_RECORD_SIZE == 29, "mq_GetBuffData's record is MQ2Mono's");
unsigned char buffData[8192];   // as MQ2Mono's: the scripts read it after the call (each call rewrites it)

unsigned char* mono_GetBuffData(int32_t* bufferLength) {
    static unsigned char slots[8192];   // 282 records: the client's 63 with room to spare
    int32_t written = 0;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_buff_data_bulk)) {
        const int32_t n = host->get_buff_data_bulk(host->host, slots, static_cast<int32_t>(sizeof slots));
        for (int32_t at = 0; n > 0 && at + HAILSTONE_BUFF_RECORD_SIZE <= n; at += HAILSTONE_BUFF_RECORD_SIZE) {
            HS_BulkBuffData record;
            std::memcpy(&record, slots + at, sizeof record);
            if (record.slotType == HAILSTONE_BUFF_SLOT_DISC) continue;
            std::memcpy(buffData + written, slots + at, HAILSTONE_BUFF_RECORD_SIZE);
            written += HAILSTONE_BUFF_RECORD_SIZE;
        }
    }
    if (bufferLength) *bufferLength = written;   // 0: no profile yet, no 'read', or a thread Hailstone won't answer on
    return buffData;
}

// mq_GetPetBuffData: the pet window's buffs, MQ2Mono's 12 bytes each: i32 spell id, tics left, spell type.
// get_pet_buff_data_bulk writes that record (HS_BulkPetBuffData), one per buff the window shows, in the order the
// server sent them (not by slot, and no empty slots between).
// E3 reads e3util.MaxPetBuffSlots (30) records whenever the length is above 0, without checking it, and skips a spell
// id of 0. So when there is at least one buff the list is padded with all-zero records to 30, and E3 never reads past
// what was written. Whether MQ2Mono's list holds the window's empty slots too is not confirmed here; zero records read
// as empty to every reader that skips id 0, as E3 does.
// The list comes from OP_PetBuffWindow, and a stock server sends at most 30 slots in it (EQEmu's
// Mob::SendPetBuffsToClient, PET_BUFF_COUNT), so the padded list is the whole window. The client's decoder takes up to
// 97 slots (kMaxSpawnBuffSlots), which the buffer has room for: a server that sends more has all of them copied.
static_assert(sizeof(HS_BulkPetBuffData) == 12 && HAILSTONE_PET_BUFF_RECORD_SIZE == 12, "mq_GetPetBuffData's record is MQ2Mono's");
constexpr int32_t kPetBuffPadRecords = 30;   // E3's e3util.MaxPetBuffSlots
unsigned char petBuffData[2048];   // 170 records: the decoder's 97 with room to spare; the scripts read it after the call

unsigned char* mono_GetPetBuffData(int32_t* bufferLength) {
    int32_t written = 0;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_pet_buff_data_bulk)) {
        const int32_t n = host->get_pet_buff_data_bulk(host->host, petBuffData, static_cast<int32_t>(sizeof petBuffData));
        if (n > 0) {
            written = n - n % HAILSTONE_PET_BUFF_RECORD_SIZE;
            constexpr int32_t padded = kPetBuffPadRecords * HAILSTONE_PET_BUFF_RECORD_SIZE;
            if (written < padded) {
                std::memset(petBuffData + written, 0, static_cast<size_t>(padded - written));
                written = padded;
            }
        }
    }
    if (bufferLength) *bufferLength = written;   // 0: no pet buffs, no 'read', or a thread Hailstone won't answer on
    return petBuffData;
}

// mq_GetTargetBuffData(spawnId): MQ2Mono's u8 "buffs populated", then one record per buff of the target: i32 spell id,
// tics left, spell type, caster name length, then that many bytes of the caster's name (no NUL). E3 reads it that way
// (Casting.cs TimeLeftOnMySpell, HudHubWindow), always for ${Target.ID}.
// get_target_buff_data_bulk has a list header and one more field per record (the buff's slot, between the spell type
// and the name's length), so each record is rewritten without it.
// The client holds buffs for the current target only, and only once the server has sent them (EQEmu sends them to
// some players: GMs, Inspect Buffs, leadership, or AlwaysSendTargetsBuffs); the bulk read is empty otherwise. So the
// list is always the current target's: spawnId is not checked (E3 passes ${Target.ID}). With no records nothing is
// returned (length 0), whether the target has no buffs or they were not sent; else the flag is 1. E3 reads the flag
// only to step past it.
static_assert(sizeof(HS_BulkTargetBuffFixedRecord) == HAILSTONE_TARGET_BUFF_RECORD_SIZE && HAILSTONE_TARGET_BUFF_RECORD_SIZE == 20,
    "the client's target buff record");
static_assert(sizeof(HS_BulkListHeader) == HS_BULK_LIST_HEADER_SIZE, "the list header");
unsigned char targetBuffData[8192];   // as MQ2Mono's: the scripts read it after the call (each call rewrites it)

unsigned char* mono_GetTargetBuffData(int32_t /*spawnId*/, int32_t* bufferLength) {
    size_t written = 0;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_target_buff_data_bulk)) {
        targetBuffData[written++] = 1;   // the flag; nothing is returned without a record
        // A stock server's worst case is 4988 bytes (60 NPC buff slots, 63-byte names); a longer list (the
        // header's total past its count) is read once more into a buffer big enough.
        static std::vector<unsigned char> list(8192);
        HS_BulkListHeader header{};
        int32_t filled = 0;
        for (int attempt = 0; attempt < 2; ++attempt) {
            filled = host->get_target_buff_data_bulk(host->host, list.data(), static_cast<int32_t>(list.size()));
            if (filled < HS_BULK_LIST_HEADER_SIZE) { filled = 0; break; }
            std::memcpy(&header, list.data(), sizeof header);
            if (header.count >= header.total) break;
            list.resize(list.size() * 2);
        }
        size_t at = HS_BULK_LIST_HEADER_SIZE;
        for (int32_t i = 0; filled > 0 && i < header.count; ++i) {
            if (at + HAILSTONE_TARGET_BUFF_RECORD_SIZE > static_cast<size_t>(filled)) break;
            HS_BulkTargetBuffFixedRecord record;
            std::memcpy(&record, list.data() + at, sizeof record);
            at += HAILSTONE_TARGET_BUFF_RECORD_SIZE;
            const size_t nameBytes = record.casterNameLength > 0 ? static_cast<size_t>(record.casterNameLength) : 0;
            if (at + nameBytes > static_cast<size_t>(filled)) break;
            if (written + 16 + nameBytes > sizeof targetBuffData) break;   // whole records only
            const int32_t fields[4] = { record.spellID, record.duration, record.spellType,
                                        static_cast<int32_t>(nameBytes) };
            std::memcpy(targetBuffData + written, fields, sizeof fields);
            written += sizeof fields;
            if (nameBytes) std::memcpy(targetBuffData + written, list.data() + at, nameBytes);
            written += nameBytes;
            at += nameBytes;
        }
        if (written == 1) written = 0;
    }
    if (bufferLength) *bufferLength = static_cast<int32_t>(written);   // 0: no target buffs, no 'read', or a thread Hailstone won't answer on
    return targetBuffData;
}

// mq_GetSpawns3_Delta: what changes about each spawn in the zone, MQ2Mono's 60 bytes a spawn, as E3's
// Spawns.RefreshList reads it: i32 id, u8 aggressive, u8 dead, f32 heading, f32 height, i32 master, u8 moving,
// i64 HP percent, i32 pet id, u8 targetable, i32 target of target, f32 x, y, z, then the player's x, y, z (the same
// in every record). E3 takes an empty list, or an id it does not know, as the cue to read every spawn again
// (mq_GetSpawns3).
// From get_spawns_data_bulk's records: the flags, heading, height, master, HP percent and position as they are. The pet
// id and target of target are 0: MQ2Mono's came from MacroQuest's player object (which extends the spawn), and do
// not apply to the spawn list.
static_assert(sizeof(HS_BulkSpawnHeader) == HS_BULK_SPAWN_HEADER_SIZE && HS_BULK_SPAWN_HEADER_SIZE == 16, "the spawn header");
static_assert(sizeof(HS_BulkSpawnFixedRecord) == HS_BULK_SPAWN_RECORD_SIZE && HS_BULK_SPAWN_RECORD_SIZE == 92,
    "the spawn record");
constexpr size_t kSpawnDeltaRecord = 60;   // MQ2Mono's
unsigned char spawnsDelta[300000];   // 5000 spawns; the scripts read it after the call (each call rewrites it)

unsigned char* mono_GetSpawns3_Delta(int32_t* bufferLength) {
    size_t written = 0;
    // get_spawns_data_bulk's records (92 bytes and seven strings each) go into the host's bulk buffer (2 MiB), held
    // only while they are rewritten into the delta.
    int32_t bulkSize = 0;
    unsigned char* spawnsBulk = nullptr;
    if (MayCallHost() && HAILSTONE_HOST_HAS(host, get_spawns_data_bulk) && HAILSTONE_HOST_HAS(host, acquire_bulk_buffer))
        spawnsBulk = host->acquire_bulk_buffer(host->host, &bulkSize);
    if (spawnsBulk) {
        const int32_t filled = host->get_spawns_data_bulk(host->host, spawnsBulk, bulkSize);
        HS_BulkSpawnHeader header{};
        if (filled >= HS_BULK_SPAWN_HEADER_SIZE) std::memcpy(&header, spawnsBulk, sizeof header);
        auto put = [&](const auto& v) { std::memcpy(spawnsDelta + written, &v, sizeof v); written += sizeof v; };
        size_t at = HS_BULK_SPAWN_HEADER_SIZE;
        for (uint32_t i = 0; filled > 0 && i < header.spawnCount; ++i) {
            if (at + HS_BULK_SPAWN_RECORD_SIZE > static_cast<size_t>(filled)) break;
            if (written + kSpawnDeltaRecord > sizeof spawnsDelta) break;
            HS_BulkSpawnFixedRecord r;
            std::memcpy(&r, spawnsBulk + at, sizeof r);
            at += HS_BULK_SPAWN_RECORD_SIZE;
            for (int s = 0; s < 7; ++s) {   // its seven strings, stepped over to the next record
                uint16_t len = 0;
                if (at + sizeof len > static_cast<size_t>(filled)) break;
                std::memcpy(&len, spawnsBulk + at, sizeof len);
                at += sizeof len + len;
            }
            put(r.id);
            put(static_cast<uint8_t>((r.flags & HS_BULK_SPAWN_AGGRESSIVE) != 0));
            put(static_cast<uint8_t>((r.flags & HS_BULK_SPAWN_DEAD) != 0));
            put(r.heading);
            put(r.height);
            put(r.master);
            put(static_cast<uint8_t>((r.flags & HS_BULK_SPAWN_MOVING) != 0));
            put(static_cast<int64_t>(r.pctHPs));
            put(int32_t{0});   // pet id
            put(static_cast<uint8_t>((r.flags & HS_BULK_SPAWN_TARGETABLE) != 0));
            put(int32_t{0});   // target of target
            put(r.x);
            put(r.y);
            put(r.z);
            put(header.playerX);
            put(header.playerY);
            put(header.playerZ);
        }
        host->release_bulk_buffer(host->host);
    }
    if (bufferLength) *bufferLength = static_cast<int32_t>(written);   // 0: no spawns, no 'read', or a thread Hailstone won't answer on
    return spawnsDelta;
}

// mq_GetSpawns3: every spawn in the zone, one at a time. Each spawn is written to spawns3Buffer, then the calling
// script's OnSetSpawnsViaCallback runs and copies it out with mq_GetSpawns3_Buffer, then the next spawn is written, as
// MQ2Mono did. E3 calls it from Spawns.RefreshList, after emptying its spawn list, and adds each spawn as its callback
// gets it.
// The record is MQ2Mono's, as E3's Spawn.Init reads it, field by field in this order (strings as an i32 length, then
// that many bytes, no NUL):
//   i32 id, u8 afk, u8 aggressive, u8 anonymous, i32 blind, i32 body type, str body type, u8 buyer, i32 class,
//   str clean name, i32 con colour, i32 endurance, i64 HP, i32 mana, u8 dead, str display name, u8 ducking,
//   u8 feigning, i32 gender, u8 GM, i64 guild, f32 heading, f32 height, u8 invis, u8 summoned, i32 level, u8 levitating,
//   u8 linkdead, f32 look, i32 master, i32 max endurance, f32 max range, f32 max range to, u8 mount, u8 moving,
//   str name, u8 named, i64 HP percent, i32 mana percent, i32 pet id, i32 player state, i32 race, str race name,
//   u8 role playing, u8 sitting, u8 sneaking, u8 standing, u8 stunned, str suffix, u8 targetable,
//   i32 target of target, u8 trader, str type, u8 underwater, f32 x, y, z, f32 the player's x, y, z, i32 deity.
// From get_spawns_data_bulk's records (the flags, numbers and its seven strings, in the same order). Not in them, and 0:
// blind, endurance, mana, look, max endurance, mana percent; the pet id and target of target are MacroQuest's player
// object's, not a spawn's. The HP is the bulk record's (the client knows a spawn's HP as a percent).
// The bulk read goes into the host's bulk buffer, held until the last spawn's callback returns: a script's own call
// that takes the bulk buffer (mq_GetSpawns3_Delta) from inside the callback would overwrite the list. A callback
// that throws ends the list (the exception is reported once, as any call's).
// A string is cut to 512 bytes, so a record always fits the 4096-byte buffer: names come from the server uncapped.
unsigned char spawns3Buffer[4096];
int32_t spawns3Length = 0;

// mq_Memory_GetPageFileSize: the process's private memory in MB (E3 publishes it as ${Me.Memory_EQPageFile}). On
// Windows the page file usage, as MQ2Mono's name says; on Linux and macOS the nearest they have (PrivateMemoryBytes).
// The whole client's: the game, Hailstone, Mono and every script. 0 when the OS won't say.
double mono_Memory_GetPageFileSize() {
    return static_cast<double>(PrivateMemoryBytes()) / (1024.0 * 1024.0);
}

// mq_GetFocusedWindowName: the window that has the keyboard focus, "NULL" when none does (the game world has it).
// Hailstone's windows are Dear ImGui's: a UI pack's are named as the pack names them (its EQ window names), the
// client's own by their titles. E3 publishes it as ${EQ.CurrentFocusedWindowName} and E3UI shows it.
MonoString* mono_GetFocusedWindowName() {
    if (!MayCallHost()) return NewString("NULL");
    const std::string name = FocusedWindowName();
    return NewString(name.empty() ? "NULL" : name.c_str());
}

unsigned char* mono_GetSpawns3Buffer(int32_t* bufferLength) {
    if (bufferLength) *bufferLength = spawns3Length;
    return spawns3Buffer;
}

void mono_GetSpawns3() {
    monoAppDomainInfo* script = CurrentScript();
    if (!script || !script->m_OnSetSpawnsViaCallback) return;
    if (!MayCallHost() || !HAILSTONE_HOST_HAS(host, get_spawns_data_bulk) || !HAILSTONE_HOST_HAS(host, acquire_bulk_buffer))
        return;
    int32_t bulkSize = 0;
    unsigned char* bulk = host->acquire_bulk_buffer(host->host, &bulkSize);
    if (!bulk) return;
    const int32_t filled = host->get_spawns_data_bulk(host->host, bulk, bulkSize);
    HS_BulkSpawnHeader header{};
    if (filled >= HS_BULK_SPAWN_HEADER_SIZE) std::memcpy(&header, bulk, sizeof header);
    size_t at = HS_BULK_SPAWN_HEADER_SIZE;
    for (uint32_t i = 0; filled > 0 && i < header.spawnCount; ++i) {
        if (at + HS_BULK_SPAWN_RECORD_SIZE > static_cast<size_t>(filled)) break;
        HS_BulkSpawnFixedRecord r;
        std::memcpy(&r, bulk + at, sizeof r);
        at += HS_BULK_SPAWN_RECORD_SIZE;
        // BodyDesc, CleanName, DisplayName, Name, RaceName, Suffix, TypeDesc
        std::string_view text[7];
        bool whole = true;
        for (auto& t : text) {
            uint16_t len = 0;
            if (at + sizeof len > static_cast<size_t>(filled)) { whole = false; break; }
            std::memcpy(&len, bulk + at, sizeof len);
            at += sizeof len;
            if (at + len > static_cast<size_t>(filled)) { whole = false; break; }
            t = std::string_view(reinterpret_cast<const char*>(bulk + at), len);
            at += len;
        }
        if (!whole) break;

        size_t n = 0;
        auto put = [&](const auto& v) { std::memcpy(spawns3Buffer + n, &v, sizeof v); n += sizeof v; };
        auto flag = [&](uint32_t f) { put(static_cast<uint8_t>((r.flags & f) != 0)); };
        auto str = [&](std::string_view t) {
            const int32_t len = static_cast<int32_t>(std::min<size_t>(t.size(), 512));
            put(len);
            std::memcpy(spawns3Buffer + n, t.data(), static_cast<size_t>(len));
            n += static_cast<size_t>(len);
        };
        put(r.id);
        flag(HS_BULK_SPAWN_AFK);
        flag(HS_BULK_SPAWN_AGGRESSIVE);
        flag(HS_BULK_SPAWN_ANON);
        put(int32_t{0});           // blind
        put(r.bodyTypeID);
        str(text[0]);              // body type
        flag(HS_BULK_SPAWN_BUYER);
        put(r.classID);
        str(text[1]);              // clean name
        put(r.conColorID);
        put(int32_t{0});           // endurance
        put(r.currentHps);
        put(int32_t{0});           // mana
        flag(HS_BULK_SPAWN_DEAD);
        str(text[2]);              // display name
        flag(HS_BULK_SPAWN_DUCKING);
        flag(HS_BULK_SPAWN_FEIGNING);
        put(r.genderId);
        flag(HS_BULK_SPAWN_GM);
        put(r.guildID);
        put(r.heading);
        put(r.height);
        flag(HS_BULK_SPAWN_INVIS);
        flag(HS_BULK_SPAWN_SUMMONED);
        put(r.level);
        flag(HS_BULK_SPAWN_LEVITATION);
        flag(HS_BULK_SPAWN_LINKDEAD);
        put(0.0f);                 // look
        put(r.master);
        put(int32_t{0});           // max endurance
        put(r.maxRange);
        put(r.maxRangeTo);
        flag(HS_BULK_SPAWN_MOUNT);
        flag(HS_BULK_SPAWN_MOVING);
        str(text[3]);              // name
        flag(HS_BULK_SPAWN_NAMED);
        put(static_cast<int64_t>(r.pctHPs));
        put(int32_t{0});           // mana percent
        put(int32_t{0});           // pet id
        put(r.playerState);
        put(r.raceID);
        str(text[4]);              // race name
        flag(HS_BULK_SPAWN_ROLEPLAYING);
        flag(HS_BULK_SPAWN_SITTING);
        flag(HS_BULK_SPAWN_SNEAKING);
        flag(HS_BULK_SPAWN_STANDING);
        flag(HS_BULK_SPAWN_STUNNED);
        str(text[5]);              // suffix
        flag(HS_BULK_SPAWN_TARGETABLE);
        put(int32_t{0});           // target of target
        flag(HS_BULK_SPAWN_TRADER);
        str(text[6]);              // type
        flag(HS_BULK_SPAWN_UNDERWATER);
        put(r.x);
        put(r.y);
        put(r.z);
        put(header.playerX);
        put(header.playerY);
        put(header.playerZ);
        put(r.deity);
        spawns3Length = static_cast<int32_t>(n);
        if (!Invoke(*script, script->m_OnSetSpawnsViaCallback, "OnSetSpawnsViaCallback")) break;
    }
    spawns3Length = 0;
    host->release_bulk_buffer(host->host);
}

} // namespace

// mono_runtime.h. The namespace and classes are fixed by the C# side: MonoCore.Core here, MonoCore.E3ImGUI in
// hsmono_imgui.cpp. Each line stays commented out until its mono_* function is in this file (above this point, or
// declared there).
void RegisterInternalCalls() {
    HSMONO_ICALL("MonoCore.Core::mq_Echo", mono_Echo);
    HSMONO_ICALL("MonoCore.Core::mq_ParseTLO", mono_ParseTLO);
    HSMONO_ICALL("MonoCore.Core::mq_DoCommand", mono_DoCommand);
    HSMONO_ICALL("MonoCore.Core::mq_DoCommandDelayed", mono_DoCommandDelayed);
    HSMONO_ICALL("MonoCore.Core::mq_Delay", mono_Delay);
    HSMONO_ICALL("MonoCore.Core::mq_AddCommand", mono_AddCommand);
    HSMONO_ICALL("MonoCore.Core::mq_ClearCommands", mono_ClearCommands);
    HSMONO_ICALL("MonoCore.Core::mq_RemoveCommand", mono_RemoveCommand);
    HSMONO_ICALL("MonoCore.Core::mq_GetSpellDataEffectCount", mono_GetSpellDataEffectCount);
    HSMONO_ICALL("MonoCore.Core::mq_GetSpellDataEffect", mono_GetSpellDataEffect);
    // HSMono's own: whole spell records (get_spells_data_bulk's layout), for scripts moving off the two above.
    HSMONO_ICALL("MonoCore.Core::mq_GetSpellsData", mono_GetSpellsData);
    HSMONO_ICALL("MonoCore.Core::mq_GetSpellsDataByName", mono_GetSpellsDataByName);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns", mono_GetSpawns);
    // HSMONO_ICALL("MonoCore.Core::mq_GetSpawns2", mono_GetSpawns2);

    HSMONO_ICALL("MonoCore.Core::mq_GetSpawns3", mono_GetSpawns3);
    HSMONO_ICALL("MonoCore.Core::mq_GetSpawns3_Buffer", mono_GetSpawns3Buffer);
    // HSMONO_ICALL("MonoCore.Core::mq_GetOnAddSpawnsBuffer", mono_GetOnAddSpawnsBuffer);

    HSMONO_ICALL("MonoCore.Core::mq_GetBuffData", mono_GetBuffData);
    HSMONO_ICALL("MonoCore.Core::mq_GetPetBuffData", mono_GetPetBuffData);
    HSMONO_ICALL("MonoCore.Core::mq_GetTargetBuffData", mono_GetTargetBuffData);
    HSMONO_ICALL("MonoCore.Core::mq_GetSpawns3_Delta", mono_GetSpawns3_Delta);
    HSMONO_ICALL("MonoCore.Core::mq_GetXtargetInfo", mono_GetXtargetInfo);
    HSMONO_ICALL("MonoCore.Core::mq_GetAvailableAAIds", mono_GetAvailableAAIds);
    HSMONO_ICALL("MonoCore.Core::mq_GetAvilableDiscIds", mono_GetAvilableDiscIds);
    HSMONO_ICALL("MonoCore.Core::mq_GetSpellIds", mono_GetSpellIds);

    // HSMONO_ICALL("MonoCore.Core::mq_GetRunNextCommand", mono_GetRunNextCommand);
    HSMONO_ICALL("MonoCore.Core::mq_GetFocusedWindowName", mono_GetFocusedWindowName);
    // HSMONO_ICALL("MonoCore.Core::mono_GetHoverWindowName", mono_GetHoverWindowName);
    HSMONO_ICALL("MonoCore.Core::mq_GetMQ2MonoVersion", mono_GetMQ2MonoVersion);   // legacy: mq_GetHSMonoVersion
    HSMONO_ICALL("MonoCore.Core::mq_GetHSMonoVersion", mono_GetHSMonoVersion);   // HSMono's own

    // HSMONO_ICALL("MonoCore.Core::mq_ExecuteCommandByName", mono_ExecuteCommandByName);
    // HSMONO_ICALL("MonoCore.Core::mq_ExecuteCommand", mono_ExecuteCommand);
    // HSMONO_ICALL("MonoCore.Core::mq_LookAt", mono_LookAt);
    HSMONO_ICALL("MonoCore.Core::mq_Memory_GetPageFileSize", mono_Memory_GetPageFileSize);

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
