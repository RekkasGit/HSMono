#pragma once
#include "hailstone_plugin.h"
#include "hailstone_plugin.hpp"
#include "mono_runtime.h"   // Mono's types, and its functions through `mono` (no import library is linked)
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <string>
#include <chrono>
#include <map>
#include <algorithm>    // std::equal in ci_equals
#include <array>        // std::array in m_IMGUI_InputColorValues
#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <string_view>  // ci_equals' parameters

inline bool ci_equals(std::string_view a, std::string_view b) {
	auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
		[&](char x, char y) { return lower(x) == lower(y); });
}

// Shared data structures and globals used across MQ2Mono compilation units

struct monoAppDomainInfo
{
	std::string m_appDomainName;
	// app domain we have created for e3
	MonoDomain* m_appDomain = nullptr;
	// core.dll information so we can bind to it
	MonoAssembly* m_csharpAssembly = nullptr;
	MonoImage* m_coreAssemblyImage = nullptr;
	MonoClass* m_classInfo = nullptr;
	MonoObject* m_classInstance = nullptr;
	uint32_t m_classInstanceHandle = 0; // a pinned GC handle: keeps m_classInstance alive and where it is
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

	bool AddScriptCommand(std::string commandName)
	{

		if (host->command_exists(host->host, commandName.c_str()) == HAILSTONE_COMMAND_NONE)
		{
			m_CommandList.push_back(commandName);
			hs::AddCommand(commandName, [this, commandName](const char* args)
				{
					if (this->m_appDomain)
					{
						if (this->m_OnCommand)
						{
							std::string line = args;
							line = commandName + " " + line;
							mono.mono_domain_set(this->m_appDomain, false);
							MonoString* monoLine = mono.mono_string_new(this->m_appDomain, line.c_str());
							void* params[1] =
							{
								monoLine
							};
							mono.mono_runtime_invoke(this->m_OnCommand, this->m_classInstance, params, nullptr);
						}
					}
				});


			return true;
		}
		return false;
	}

	void RemoveCommand(std::string command)
	{
		int count = 0;
		int processCount = static_cast<int>(this->m_CommandList.size());
		while (count < processCount)
		{
			count++;
			std::string currentKey = this->m_CommandList.front();
			m_CommandList.pop_front();
			if (!ci_equals(currentKey, command))
			{
				m_CommandList.push_back(currentKey);
			}
			else
			{
				hs::RemoveCommand(currentKey.c_str());
			}
		}
	}

	void ClearCommands()
	{
		while (this->m_CommandList.size() > 0)
		{
			//mq::RemoveCommand(m_CommandList.front().c_str());
			m_CommandList.pop_front();
		}
	}
	hs::Commands commands_;// last: its commands go before the members they capture, also prevents  the compiler from letting this move at all, so a safey check.
};

// Globals defined in MQ2Mono.cpp, used by ImGui wrappers
extern std::map<std::string, monoAppDomainInfo> monoAppDomains;
extern std::map<MonoDomain*, std::string> monoAppDomainPtrToString;
