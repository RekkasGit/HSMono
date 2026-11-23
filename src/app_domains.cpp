// Scripts: loading a C# assembly into an app domain of its own, unloading it, and every call into C#.
// Follows MQ2Mono's InitAppDomain, UnloadAppDomain and UnloadAllAppDomains.
#include "hsmono.hpp"

#include <atomic>
#include <filesystem>
#include <system_error>
#include <thread>

std::map<std::string, monoAppDomainInfo> monoAppDomains;
std::map<MonoDomain*, std::string> monoAppDomainPtrToString;
std::deque<std::string> appDomainProcessQueue;

namespace fs = std::filesystem;

namespace {

// The C# side's entry class: namespace MonoCore, class Core.
constexpr const char* kCoreNamespace = "MonoCore";
constexpr const char* kCoreClass = "Core";
// MQ2Mono gives every script's domain this config file name, whatever the script is called.
constexpr const char* kDomainConfig = "e3.config";

int invokeDepth = 0;   // calls into C# in progress (Invoke nests when a script's command runs inside another's call)
// The client's thread is lent to the scripts' own threads (hs::LendThread around the outermost Invoke). Read from
// those threads.
std::atomic<bool> clientThreadLent{false};

bool IsFile(const fs::path& p) {
    std::error_code ec;   // nothing may throw out of a command
    return fs::is_regular_file(p, ec);
}

// A name that is safe as one folder or file name: no separators, no "..", nothing a file system treats specially.
bool IsPathComponent(std::string_view s) {
    if (s.empty() || s.size() > 64 || s.find("..") != std::string_view::npos) return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

std::map<std::string, monoAppDomainInfo>::iterator FindScript(std::string_view name) {
    for (auto it = monoAppDomains.begin(); it != monoAppDomains.end(); ++it)
        if (ci_equals(it->first, name)) return it;
    return monoAppDomains.end();
}

// MQ2Mono's shadow copy: the script's files go to <script folder>/<character>/ and load from there, so the originals
// stay unlocked (rebuild while it runs) and boxed characters each have their own copy. Files only, not subfolders
// (the character folders themselves live there); a file is copied when the original is newer.
std::optional<std::string> ShadowCopy(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) return "cannot create " + PathUtf8(to) + " (" + ec.message() + ")";
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec)) continue;
        const fs::path target = to / it->path().filename();
        fs::copy_file(it->path(), target, fs::copy_options::update_existing, ec);
        if (ec) return "cannot copy to " + PathUtf8(target) + " (" + ec.message() + "); is it locked?";
    }
    if (ec) return "cannot read " + PathUtf8(from) + " (" + ec.message() + ")";
    return std::nullopt;
}

void Unload(std::map<std::string, monoAppDomainInfo>::iterator it) {
    monoAppDomainInfo& d = it->second;
    Invoke(d, d.m_OnStop, "OnStop");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));   // as MQ2Mono: give the script's threads time to end
    d.ClearCommands();

    MonoDomain* domain = d.m_appDomain;
    if (d.m_classInstanceHandle) mono.mono_gchandle_free(d.m_classInstanceHandle);
    std::erase_if(appDomainProcessQueue, [&](const std::string& key) { return ci_equals(key, it->first); });
    monoAppDomainPtrToString.erase(domain);
    monoAppDomains.erase(it);

    mono.mono_domain_set(g_rootDomain, false);   // a domain can't be unloaded while it is the current one
    if (domain && domain != g_rootDomain) mono.mono_domain_unload(domain);
}

} // namespace

std::optional<MonoObject*> Invoke(monoAppDomainInfo& script, MonoMethod* method, const char* what, void** params) {
    if (!method || !script.m_appDomain) return std::optional<MonoObject*>(nullptr);   // nothing to call: not a throw
    // The outermost call lends the client's thread to the scripts' threads while it waits: E3's MainProcessor runs on
    // a thread of its own, while OnPulse blocks, and reads TLOs and runs commands from there (mq_ParseTLO ...). A
    // nested call (a script's command inside another's call, a TLO a lent thread reads) is inside that lending.
    std::optional<hs::LendThread> lend;
    if (invokeDepth == 0) {
        lend.emplace();
        clientThreadLent.store(lend->ok());
    }
    MonoDomain* previous = mono.mono_domain_get();
    MonoObject* exception = nullptr;
    ++invokeDepth;
    mono.mono_domain_set(script.m_appDomain, false);
    MonoObject* result = mono.mono_runtime_invoke(method, script.m_classInstance, params, &exception);
    mono.mono_domain_set(previous ? previous : g_rootDomain, false);
    --invokeDepth;
    if (lend) clientThreadLent.store(false);   // before ~LendThread ends it (which waits for a host call in flight)
    if (!exception) return result;
    if (!script.m_exceptionReported) {
        script.m_exceptionReported = true;
        WriteChatf("[HSMono] %s: %s threw an exception (further ones are not reported)", script.m_appDomainName.c_str(),
                   what);
    }
    return std::nullopt;
}

monoAppDomainInfo* CurrentScript() {
    auto name = monoAppDomainPtrToString.find(mono.mono_domain_get());
    if (name == monoAppDomainPtrToString.end()) return nullptr;
    auto it = monoAppDomains.find(name->second);
    return it == monoAppDomains.end() ? nullptr : &it->second;
}

bool InScriptCall() { return invokeDepth > 0; }

bool ClientThreadLent() { return clientThreadLent.load(); }

std::optional<std::string> ScriptName(std::string_view typed) {
    if (typed.size() > 4 && ci_equals(typed.substr(typed.size() - 4), ".dll")) typed.remove_suffix(4);
    if (!IsPathComponent(typed)) return std::nullopt;
    return std::string(typed);
}

std::optional<std::string> LoadScript(const std::string& name) {
    // The shadow folder is the character's, and OnPulse unloads every script outside the game.
    const std::optional<HailstonePlayer> player = hs::GetPlayer();
    if (!player || player->game_state != GAMESTATE_INGAME) return std::string("scripts run only in game");
    const std::string character = player->name;
    if (!IsPathComponent(character)) return "cannot use the character name '" + character + "' as a folder";

    // As MQ2Mono: loading a script that runs restarts it.
    if (auto it = FindScript(name); it != monoAppDomains.end()) Unload(it);

    const fs::path source = PluginConfigDir() / "macros" / name;
    const std::string fileName = name + ".dll";
    if (!IsFile(source / fileName)) return "Cannot find " + PathUtf8(source / fileName);
    const fs::path shadow = source / character;
    if (std::optional<std::string> error = ShadowCopy(source, shadow)) return error;

    std::string friendlyName = name;   // Mono's signature takes char*
    MonoDomain* domain = mono.mono_domain_create_appdomain(friendlyName.data(), nullptr);
    if (!domain) return "could not create an app domain for " + name;
    auto fail = [&](std::string why) {
        mono.mono_domain_set(g_rootDomain, false);
        mono.mono_domain_unload(domain);
        return std::optional<std::string>(std::move(why));
    };

    mono.mono_domain_set(domain, false);
    const std::string shadow8 = PathUtf8(shadow / "");   // with its trailing separator, as MQ2Mono gives it
    mono.mono_domain_set_config(domain, shadow8.c_str(), kDomainConfig);
    const std::string dll8 = PathUtf8(shadow / fileName);
    MonoAssembly* assembly = mono.mono_domain_assembly_open(domain, dll8.c_str());
    if (!assembly) return fail("could not load " + dll8);
    MonoImage* image = mono.mono_assembly_get_image(assembly);
    MonoClass* cls = image ? mono.mono_class_from_name(image, kCoreNamespace, kCoreClass) : nullptr;
    if (!cls) return fail(fileName + " has no class " + kCoreNamespace + "." + kCoreClass);
    // As MQ2Mono: allocated, its constructor not run (E3's Core has never had it run).
    MonoObject* instance = mono.mono_object_new(domain, cls);
    if (!instance) return fail("could not create " + std::string(kCoreNamespace) + "." + kCoreClass);
    // Pinned at once: the GC may move or collect an object only C++ refers to.
    const uint32_t handle = mono.mono_gchandle_new(instance, true);

    monoAppDomainInfo& d = monoAppDomains.try_emplace(name).first->second;
    d.m_appDomainName = name;
    d.m_appDomain = domain;
    d.m_csharpAssembly = assembly;
    d.m_coreAssemblyImage = image;
    d.m_classInfo = cls;
    d.m_classInstance = instance;
    d.m_classInstanceHandle = handle;
    // MQ2Mono's names and parameter counts; a method the C# class lacks stays null.
    auto method = [cls](const char* n, int params) { return mono.mono_class_get_method_from_name(cls, n, params); };
    d.m_OnPulseMethod = method("OnPulse", 0);
    d.m_OnWriteChatColor = method("OnWriteChatColor", 1);
    d.m_OnIncomingChat = method("OnIncomingChat", 1);
    d.m_OnInit = method("OnInit", 0);
    d.m_OnStop = method("OnStop", 0);
    d.m_OnUpdateImGui = method("OnUpdateImGui", 0);
    d.m_OnCommand = method("OnCommand", 1);
    d.m_OnSetSpawns = method("OnSetSpawns", 2);
    d.m_OnSetSpawnsViaCallback = method("OnSetSpawnsViaCallback", 0);
    d.m_OnQuery = method("OnQuery", 1);
    d.m_OnSpawnAdd = method("OnSpawnAdd", 0);
    d.m_OnZoned = method("OnZoned", 0);

    monoAppDomainPtrToString[domain] = name;
    appDomainProcessQueue.push_back(name);
    mono.mono_domain_set(g_rootDomain, false);

    if (!Invoke(d, d.m_OnInit, "OnInit")) {
        Unload(monoAppDomains.find(name));
        return name + ": OnInit threw an exception; unloaded";
    }
    return std::nullopt;
}

bool UnloadScript(std::string_view name) {
    auto it = FindScript(name);
    if (it == monoAppDomains.end()) return false;
    Unload(it);
    return true;
}

void UnloadAllScripts() {
    while (!monoAppDomains.empty()) Unload(monoAppDomains.begin());
}

std::string QueryScript(std::string_view name, const std::string& expression) {
    auto it = FindScript(name);
    if (it == monoAppDomains.end() || !it->second.m_OnQuery) return {};
    monoAppDomainInfo& d = it->second;
    MonoString* monoExpression = mono.mono_string_new(d.m_appDomain, expression.c_str());
    void* params[1] = {monoExpression};
    const std::optional<MonoObject*> result = Invoke(d, d.m_OnQuery, "OnQuery", params);
    if (!result || !*result) return {};
    char* utf8 = mono.mono_string_to_utf8(reinterpret_cast<MonoString*>(*result));   // OnQuery returns a string
    std::string answer = utf8 ? utf8 : "";
    if (utf8) mono.mono_free(utf8);
    return answer;
}

// ---- a script's commands

bool monoAppDomainInfo::AddScriptCommand(const std::string& commandName) {
    for (const auto& [name, script] : monoAppDomains)
        for (const std::string& c : script.m_CommandList)
            if (ci_equals(c, commandName)) return false;   // a script's own: hs::AddCommand would replace it
    // Everyone else's, where the client can say (command_exists); otherwise AddCommand refuses the client's own.
    if (const std::optional<int> kind = hs::CommandExists(commandName); kind && *kind != HAILSTONE_COMMAND_NONE)
        return false;

    const bool added = hs::AddCommand(commandName, [this, commandName](const char* args) {
        if (!m_OnCommand) return;
        const std::string line = commandName + " " + args;
        MonoString* monoLine = mono.mono_string_new(m_appDomain, line.c_str());
        void* params[1] = {monoLine};
        Invoke(*this, m_OnCommand, "OnCommand", params);
    });
    if (added) m_CommandList.push_back(commandName);
    return added;
}

void monoAppDomainInfo::RemoveCommand(const std::string& command) {
    std::erase_if(m_CommandList, [&](const std::string& c) {
        if (!ci_equals(c, command)) return false;
        hs::RemoveCommand(c);
        return true;
    });
}

void monoAppDomainInfo::ClearCommands() {
    for (const std::string& c : m_CommandList) hs::RemoveCommand(c);
    m_CommandList.clear();
}
