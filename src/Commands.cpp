#include "Commands.hpp"

#include "TwitchBot.hpp"
#include "SongQueue.hpp"
#include "Config.hpp"
#include "OAuth.hpp"

#include "json.hpp"

#include <random>
#include <charconv>

std::unordered_map<std::string, CommandCallback> Commands::commandRegistry;

void Commands::RegisterCommands()
{
	commandRegistry["!sr"] = [&](const ChatMessage& msg, const std::vector<std::string>& args) {
		if (!SongQueue::RequestsEnabled())
		{
			TwitchBot::SendMessage("The queue is currently closed!");
			return;
		}

		if (args.empty())
		{
			TwitchBot::SendMessage("Usage: !sr <Artist - Song>");
			return;
		}

		std::string cleanQuery;

		for (const ChatSegment& seg : msg.segments)
		{
			std::visit([&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TextSegment>) { cleanQuery += arg.text; }
			}, seg);
		}

		size_t cmdPos = cleanQuery.find("!sr");
		if (cmdPos != std::string::npos) { cleanQuery = cleanQuery.substr(cmdPos + 3); }
		cmdPos = cleanQuery.find("!request");
		if (cmdPos != std::string::npos) { cleanQuery = cleanQuery.substr(cmdPos + 8); }
		cmdPos = cleanQuery.find("!songrequest");
		if (cmdPos != std::string::npos) { cleanQuery = cleanQuery.substr(cmdPos + 13); }

		cleanQuery.erase(0, cleanQuery.find_first_not_of(" \t"));
		cleanQuery.erase(cleanQuery.find_last_not_of(" \t") + 1);

		if (cleanQuery.length() < 3)
		{
			TwitchBot::SendMessage("Song request is too short, @" + msg.displayName);
			return;
		}

		SongQueue::AddToQueue({ cleanQuery, msg.displayName });
	};

	commandRegistry["!songrequest"] = commandRegistry["!sr"];
	commandRegistry["!request"] = commandRegistry["!sr"];

	commandRegistry["!list"] = [&](const ChatMessage& msg, const std::vector<std::string>& args) {
		TwitchBot::SendMessage("Browse songs and request from here: https://songs.hababot2.com/" + OAuth::ChannelName());
	};

	commandRegistry["!songs"] = commandRegistry["!list"];

	for (const auto& [cmd, responseTemplate] : Config::data.customCommands)
	{
		std::string normalized = NormalizeCommandName(cmd);
		if (!normalized.empty() && !IsBuiltInCommand(normalized))
		{
			BindCustomCommand(normalized, responseTemplate);
		}
	}
}

void Commands::RunCommand(ChatMessage& msg)
{
	std::istringstream iss(msg.text);
	std::string command;
	iss >> command;
	std::transform(command.begin(), command.end(), command.begin(), ::tolower);

	std::vector<std::string> args;
	std::string arg;
	while (iss >> arg) { args.push_back(arg); }

	if (commandRegistry.contains(command))
	{
		commandRegistry[command](msg, args);
	}
}

void Commands::RenderCommandManager()
{
	static char cmdBuf[64] = "";
	static char respBuf[512] = "";
	static std::string editingKey;
	static std::string errorMsg;

	ImGui::Text(editingKey.empty() ? "Add New Command" : "Editing: %s", editingKey.c_str());

	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputTextWithHint("##cmdName", "!command", cmdBuf, sizeof(cmdBuf));
	ImGui::SameLine();

	if (ImGui::Button(editingKey.empty() ? "Add Command" : "Save Changes"))
	{
		std::string normalized = NormalizeCommandName(cmdBuf);
		if (normalized.size() < 2)
		{
			errorMsg = "Command name cannot be empty.";
		}
		else if (IsBuiltInCommand(normalized))
		{
			errorMsg = normalized + " is a built-in command and cannot be overwritten.";
		}
		else if (strlen(respBuf) == 0)
		{
			errorMsg = "Response text cannot be empty.";
		}
		else
		{
			if (!editingKey.empty() && editingKey != normalized)
			{
				RemoveCustomCommand(editingKey);
			}

			UpsertCustomCommand(normalized, respBuf);
			cmdBuf[0] = '\0';
			respBuf[0] = '\0';
			editingKey.clear();
			errorMsg.clear();
		}
	}

	if (!editingKey.empty())
	{
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			cmdBuf[0] = '\0';
			respBuf[0] = '\0';
			editingKey.clear();
			errorMsg.clear();
		}
	}

	ImGui::InputTextMultiline("##cmdResp", respBuf, sizeof(respBuf), ImVec2(-1.0f, 70.0f));

	if (!errorMsg.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", errorMsg.c_str());
	}

	if (ImGui::CollapsingHeader("Available Keywords"))
	{
		ImGui::BulletText("{user}                - Chatter's display name");
		ImGui::BulletText("{target}            - First word after command (falls back to {user})");
		ImGui::BulletText("{args}                - Full text after the command");
		ImGui::BulletText("{channel}         - Your channel name");
		ImGui::BulletText("{x-y}                   - Random number from x to y (e.g. {1-100})");
		ImGui::BulletText("{choice:a|b|c}  - Random pick from | separated options");
		ImGui::BulletText("{queue}             - Current song queue length");
	}

	ImGui::Separator();
	ImGui::Spacing();

	std::string pendingDelete;
	if (ImGui::BeginTable("CommandsTable", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
	{
		ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("Response", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 115.0f);
		ImGui::TableHeadersRow();

		for (const auto& [cmd, response] : Config::data.customCommands)
		{
			ImGui::PushID(cmd.c_str());
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(cmd.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::TextWrapped("%s", response.c_str());

			ImGui::TableSetColumnIndex(2);
			if (ImGui::SmallButton("Edit"))
			{
				editingKey = cmd;
				strncpy_s(cmdBuf, cmd.c_str(), sizeof(cmdBuf) - 1);
				strncpy_s(respBuf, response.c_str(), sizeof(respBuf) - 1);
				errorMsg.clear();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete"))
			{
				pendingDelete = cmd;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	if (!pendingDelete.empty())
	{
		if (editingKey == pendingDelete)
		{
			cmdBuf[0] = '\0';
			respBuf[0] = '\0';
			editingKey.clear();
		}
		RemoveCustomCommand(pendingDelete);
	}
}

std::string Commands::ExpandCommandTemplate(const std::string& tmpl, const ChatMessage& msg, const std::vector<std::string>& args)
{
	static thread_local std::mt19937 rng{ std::random_device{}() };

	std::string out;
	out.reserve(tmpl.size() + 32);

	size_t i = 0;
	while (i < tmpl.size())
	{
		if (tmpl[i] == '{')
		{
			size_t close = tmpl.find('}', i + 1);
			if (close != std::string::npos)
			{
				std::string_view token(tmpl.data() + i + 1, close - i - 1);
				bool matched = true;

				if (token == "user")
				{
					out += msg.displayName;
				}
				else if (token == "channel")
				{
					out += OAuth::ChannelName();
				}
				else if (token == "queue")
				{
					out += std::to_string(SongQueue::QueueSize());
				}
				else if (token == "target")
				{
					if (!args.empty())
					{
						std::string target = args[0];
						if (!target.empty() && target[0] == '@') { target.erase(0, 1); }
						out += target.empty() ? msg.displayName : target;
					}
					else
					{
						out += msg.displayName;
					}
				}
				else if (token == "args")
				{
					for (size_t a = 0; a < args.size(); ++a)
					{
						if (a > 0) { out += ' '; }
						out += args[a];
					}
				}
				else if (token.starts_with("choice:"))
				{
					std::string_view list = token.substr(7);
					std::vector<std::string_view> options;
					size_t start = 0;
					while (start <= list.size())
					{
						size_t bar = list.find('|', start);
						if (bar == std::string_view::npos)
						{
							options.push_back(list.substr(start));
							break;
						}
						options.push_back(list.substr(start, bar - start));
						start = bar + 1;
					}

					if (!options.empty())
					{
						std::uniform_int_distribution<size_t> dist(0, options.size() - 1);
						out += options[dist(rng)];
					}
				}
				else
				{
					size_t dash = token.find('-', 1);
					int low = 0, high = 0;
					if (dash != std::string_view::npos)
					{
						auto left = token.substr(0, dash);
						auto right = token.substr(dash + 1);
						auto [p1, ec1] = std::from_chars(left.data(), left.data() + left.size(), low);
						auto [p2, ec2] = std::from_chars(right.data(), right.data() + right.size(), high);

						if (ec1 == std::errc{} && p1 == left.data() + left.size() &&
							ec2 == std::errc{} && p2 == right.data() + right.size())
						{
							if (low > high) { std::swap(low, high); }
							std::uniform_int_distribution<int> dist(low, high);
							out += std::to_string(dist(rng));
						}
						else
						{
							matched = false;
						}
					}
					else
					{
						matched = false;
					}
				}

				if (matched)
				{
					i = close + 1;
					continue;
				}
			}
		}

		out.push_back(tmpl[i++]);
	}

	return out;
}

std::string Commands::NormalizeCommandName(std::string cmd)
{
	cmd.erase(std::remove_if(cmd.begin(), cmd.end(),
		[](unsigned char c) { return std::isspace(c); }), cmd.end());

	if (cmd.empty()) { return ""; }
	if (cmd[0] != '!') { cmd.insert(cmd.begin(), '!'); }

	std::transform(cmd.begin(), cmd.end(), cmd.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return cmd;
}

bool Commands::IsBuiltInCommand(const std::string& cmd)
{
	static const std::unordered_set<std::string> builtIns = {
		"!sr", "!songrequest", "!request", "!list", "!songs"
	};
	return builtIns.contains(cmd);
}

void Commands::BindCustomCommand(const std::string& cmd, const std::string& responseTemplate)
{
	commandRegistry[cmd] = [responseTemplate](const ChatMessage& msg, const std::vector<std::string>& args) {
		std::string response = ExpandCommandTemplate(responseTemplate, msg, args);
		TwitchBot::SendMessage(response);
	};
}

bool Commands::UpsertCustomCommand(const std::string& rawCmd, const std::string& responseTemplate)
{
	std::string cmd = NormalizeCommandName(rawCmd);
	if (cmd.size() < 2 || responseTemplate.empty() || IsBuiltInCommand(cmd))
	{
		return false;
	}

	Config::data.customCommands[cmd] = responseTemplate;
	BindCustomCommand(cmd, responseTemplate);
	Config::Save();
	return true;
}

void Commands::RemoveCustomCommand(const std::string& cmd)
{
	if (IsBuiltInCommand(cmd)) { return; }

	Config::data.customCommands.erase(cmd);
	commandRegistry.erase(cmd);
	Config::Save();
}