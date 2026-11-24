#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

struct CustomCommand;
struct ChatMessage;
using CommandCallback = std::function<void(const ChatMessage&, const std::vector<std::string>&)>;

enum class CommandPermission : int
{
	Everyone = 0,
	Subscriber,
	VIP,
	Moderator,
	Broadcaster
};

class Commands
{
public:
	static void RegisterCommands();
	static void RenderCommandManager();

	static void RunCommand(ChatMessage& msg);

	static std::string ExpandCommandTemplate(const std::string& tmpl, const ChatMessage& msg, const std::vector<std::string>& args);
	static std::string NormalizeCommandName(std::string cmd);
	static bool IsBuiltInCommand(const std::string& cmd);
	static void BindCustomCommand(const std::string& cmd, const CustomCommand& customCmd);
	static bool UpsertCustomCommand(const std::string& rawCmd, const std::string& responseTemplate, CommandPermission perm);
	static void RemoveCustomCommand(const std::string& cmd);

	static bool HasCommandPermission(const ChatMessage& msg, CommandPermission required);
	static const char* PermissionToString(CommandPermission perm);
	static CommandPermission StringToPermission(std::string_view str);

private:
	static std::unordered_map<std::string, CommandCallback> commandRegistry;

	friend class TwitchBot;
};