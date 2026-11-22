/* An example Hailstone plugin in MacroQuest's style (sdk/hailstone_plugin.h): a /hello command, an
 * ${Example} TLO, the pulse, zoning, chat and packet callbacks, and a web page of its own (/webexample, ABI 3).
 * Built with the client (CMake target HSMono); a server with a hailstone.cfg may keep the player's own plugins
 * off (docs/plugins.md). */
#include <stdlib.h>
#include <string.h>

#include "hailstone_plugin.h"
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <deque>
#include <string>
#include <chrono>

HAILSTONE_PLUGIN

static int pulses, chats, zones;
static uint32_t page; /* the plugin's web tab (0: none) */
bool initialized = false;
bool previousCommand = false; //a command was issued on this call.
//used to keep a revolving list of who is valid to process. 
std::deque<std::string> appDomainProcessQueue;

static void hello(void* user, const char* args) {
    (void)user;
    WriteChatf("[HSMono] Hello, %s!", args && *args ? args : "world");
}

/* /webexample <address>: opens the plugin's own tab on it (or moves it there); /webexample post <text>: hands the
 * text to the page's window.hailstone.onmessage. What the page posts (window.hailstone.post) comes to the chat window
 * in OnPulse. A window would draw the tab with host->web_view in OnUpdateImGui. Needs the server's 'web'
 * permission, and the address must be on a site its hailstone.cfg allows. */
static void webexample(void* user, const char* args) {
   
}

/* ${Example}, ${Example.Pulses}, ${Example.Chats}, ${Example.Zones}, ${Example[x].Index}, ${Example.Twice[21]},
 * ${Example.MyHP} (the plugin reading a TLO of the client's) */
static int example_tlo(void* user, const char* index, const char* member, const char* member_index, HailstoneValue* out) {
    static char hp[32];
    (void)user;
    if (!member) {
        out->kind = HAILSTONE_STRING;
        out->s = "Example";
        return 1;
    }
    if (strcmp(member, "Pulses") == 0) {
        out->kind = HAILSTONE_INT;
        out->i = pulses;
        return 1;
    }
    if (strcmp(member, "Chats") == 0) {
        out->kind = HAILSTONE_INT;
        out->i = chats;
        return 1;
    }
    if (strcmp(member, "Zones") == 0) {
        out->kind = HAILSTONE_INT;
        out->i = zones;
        return 1;
    }
    if (strcmp(member, "Index") == 0) {
        out->kind = HAILSTONE_STRING;
        out->s = index;
        return 1;
    }
    if (strcmp(member, "Twice") == 0) {
        out->kind = HAILSTONE_INT;
        out->i = 2 * atoi(member_index);
        return 1;
    }
    if (strcmp(member, "MyHP") == 0) {
        if (ParseMacroData("${Me.PctHPs}", hp, sizeof hp) < 0) return 0;
        out->kind = HAILSTONE_INT;
        out->i = atoi(hp);
        return 1;
    }
    return 0;
}

HAILSTONE_EXPORT void InitializePlugin(void) {
    AddCommand("/hello", hello, 0);
    AddCommand("/webexample", webexample, 0);
    AddTopLevelObject("Example", example_tlo, 0);
    WriteChatf("[HSMono] loaded");
}

HAILSTONE_EXPORT void ShutdownPlugin(void) {
    RemoveCommand("/hello");
    RemoveCommand("/webexample");
    if (page) host->web_close(host->host, page); /* the client closes it anyway as the plugin unloads */
    RemoveTopLevelObject("Example");
}

HAILSTONE_EXPORT void OnPulse(void) {
  
    if (!initialized) return;
    previousCommand = false;
    if (appDomainProcessQueue.size() < 1) return;
    std::chrono::steady_clock::time_point proccessingTimer = std::chrono::steady_clock::now(); //the time this was issued + m_delayTime


    
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
