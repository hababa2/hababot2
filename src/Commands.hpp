#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

struct ChatMessage;

using CommandCallback = std::function<void(const ChatMessage&, const std::vector<std::string>&)>;

class Commands
{
	static void RegisterCommands();
	static void RenderCommandManager();

	static void RunCommand(ChatMessage& msg);

	static std::string ExpandCommandTemplate(const std::string& tmpl, const ChatMessage& msg, const std::vector<std::string>& args);
	static std::string NormalizeCommandName(std::string cmd);
	static bool IsBuiltInCommand(const std::string& cmd);
	static void BindCustomCommand(const std::string& cmd, const std::string& responseTemplate);
	static bool UpsertCustomCommand(const std::string& rawCmd, const std::string& responseTemplate);
	static void RemoveCustomCommand(const std::string& cmd);

private:
	static std::unordered_map<std::string, CommandCallback> commandRegistry;

	friend class TwitchBot;
};