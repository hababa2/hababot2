#include "Config.hpp"

#include "Window.hpp"
#include "SongLibrary.hpp"

#include "json.hpp"

#include "imgui.h"

#include <fstream>
#include <iostream>

ConfigSettings Config::data{};

std::mutex Config::mtx;

bool Config::Load(const std::string& filepath)
{
	bool fileExisted = false;

	{
		std::lock_guard<std::mutex> lock(mtx);
		SetDefaults();

		std::ifstream file(filepath);
		if (file.is_open())
		{
			fileExisted = true;
			std::stringstream buffer;
			buffer << file.rdbuf();
			std::string rawJson = buffer.str();

			for (size_t i = 0; i < rawJson.length(); ++i)
			{
				if (rawJson[i] == '\\' && i + 1 < rawJson.length() && rawJson[i + 1] != '"')
				{
					rawJson[i] = '/';
				}
			}

			try
			{
				nlohmann::json j = nlohmann::json::parse(rawJson);

				data.setupComplete = j.value("setup_complete", true);

				if (j.contains("currentSongPath")) { data.currentSongPath = j["currentSongPath"]; }

				if (j.contains("songs") && j["songs"].contains("folders"))
				{
					data.songFolders = j["songs"]["folders"].get<std::vector<std::string>>();
				}

				if (j.contains("custom_commands") && j["custom_commands"].is_object())
				{
					data.customCommands.clear();
					for (auto& [cmd, val] : j["custom_commands"].items())
					{
						if (val.is_string())
						{
							data.customCommands[cmd] = { val.get<std::string>(), CommandPermission::Everyone };
						}
						else if (val.is_object())
						{
							data.customCommands[cmd] = {
								val.value("response", ""),
								Commands::StringToPermission(val.value("permission", "everyone"))
							};
						}
					}
				}

				if (j.contains("window") && j["window"].is_object())
				{
					const auto& w = j["window"];
					data.window.left = w.value("left", data.window.left);
					data.window.top = w.value("top", data.window.top);
					data.window.right = w.value("right", data.window.right);
					data.window.bottom = w.value("bottom", data.window.bottom);
					data.window.showCmd = w.value("showCmd", data.window.showCmd);
					data.window.hasSavedPlacement = true;

					data.window.queuePanelWidth = w.value("queuePanelWidth", data.window.queuePanelWidth);
					data.window.detachQueueWindow = w.value("detachQueueWindow", false);

					if (w.contains("queueWinX") && w.contains("queueWinY") &&
						w.contains("queueWinW") && w.contains("queueWinH"))
					{
						data.window.queueWinX = w.value("queueWinX", data.window.queueWinX);
						data.window.queueWinY = w.value("queueWinY", data.window.queueWinY);
						data.window.queueWinW = std::max(220, w.value("queueWinW", data.window.queueWinW));
						data.window.queueWinH = std::max(200, w.value("queueWinH", data.window.queueWinH));
						data.window.hasSavedQueueBounds = true;
					}
				}

				if (j.contains("oauth") && j["oauth"].is_object())
				{
					data.oauthAccessToken = j["oauth"].value("access_token", "");
				}

				if (j.contains("early_birds") && j["early_birds"].is_object())
				{
					data.earlyBirds.clear();
					for (auto& [user, stats] : j["early_birds"].items())
					{
						data.earlyBirds[user] = {
							stats.value("first", 0),
							stats.value("second", 0),
							stats.value("third", 0)
						};
					}
				}

				if (j.contains("queue") && j["queue"].is_object())
				{
					const auto& q = j["queue"];
					data.queue.requestsEnabled = q.value("requests_enabled", true);
					data.queue.queueVisible = q.value("queue_visible", true);
					data.queue.items.clear();

					if (q.contains("items") && q["items"].is_array())
					{
						for (const auto& item : q["items"])
						{
							std::string req = item.value("request", "");
							std::string user = item.value("requester", "Anonymous");
							int64_t len = item.value("length", int64_t{ 0 });
							if (!req.empty())
							{
								data.queue.items.push_back({ std::move(req), std::move(user), len });
							}
						}
					}
				}

				data.themeName = j.value("theme", "wisteria");
			}
			catch (const std::exception& e)
			{
				std::cerr << "[Config] Failed to parse config: " << e.what() << "\n";
			}
		}
	}

	return fileExisted && data.setupComplete;
}

void Config::Save(const std::string& filepath)
{
	std::lock_guard<std::mutex> lock(mtx);

	nlohmann::json j;
	j["setup_complete"] = data.setupComplete;
	j["currentSongPath"] = data.currentSongPath;
	j["songs"]["folders"] = data.songFolders;
	j["custom_commands"] = nlohmann::json::object();

	for (const auto& [cmd, customCmd] : data.customCommands)
	{
		j["custom_commands"][cmd] = {
			{ "response", customCmd.response },
			{ "permission", Commands::PermissionToString(customCmd.permission) }
		};
	}

	if (data.window.hasSavedPlacement)
	{
		j["window"] = {
			{ "showCmd", data.window.showCmd },
			{ "left", data.window.left },
			{ "top", data.window.top },
			{ "right", data.window.right },
			{ "bottom", data.window.bottom },
			{ "queuePanelWidth", data.window.queuePanelWidth },
			{ "detachQueueWindow", data.window.detachQueueWindow }
		};

		if (data.window.hasSavedQueueBounds)
		{
			j["window"]["queueWinX"] = data.window.queueWinX;
			j["window"]["queueWinY"] = data.window.queueWinY;
			j["window"]["queueWinW"] = data.window.queueWinW;
			j["window"]["queueWinH"] = data.window.queueWinH;
		}
	}

	if (!data.oauthAccessToken.empty())
	{
		j["oauth"] = {
			{ "access_token", data.oauthAccessToken }
		};
	}

	j["early_birds"] = nlohmann::json::object();
	for (const auto& [user, stats] : data.earlyBirds)
	{
		j["early_birds"][user] = {
			{ "first", stats.first },
			{ "second", stats.second },
			{ "third", stats.third }
		};
	}

	nlohmann::json queueItems = nlohmann::json::array();
	for (const auto& item : data.queue.items)
	{
		queueItems.push_back({
			{ "request", item.request },
			{ "requester", item.requester },
			{ "length", item.lengthMs }
		});
	}

	j["queue"] = {
		{ "requests_enabled", data.queue.requestsEnabled },
		{ "queue_visible", data.queue.queueVisible },
		{ "items", std::move(queueItems) }
	};

	j["theme"] = data.themeName;

	std::filesystem::path targetPath(filepath);
	std::filesystem::path tmpPath = targetPath;
	tmpPath += ".tmp";

	std::ofstream file(tmpPath);
	if (file.is_open())
	{
		file << j.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
		file.close();

		std::error_code ec;
		std::filesystem::rename(tmpPath, targetPath, ec);
	}
}

void Config::SetDefaults()
{
	data = ConfigSettings{};
	data.currentSongPath = "Documents/Clone Hero/currentsong.txt";
	std::string defaultSongsPath = "Documents/Clone Hero/Songs";

	char* userProfile = nullptr;
	size_t len = 0;
	_dupenv_s(&userProfile, &len, "USERPROFILE");

	if (userProfile)
	{
		data.currentSongPath = std::string(userProfile) + "/Documents/Clone Hero/currentsong.txt";
		defaultSongsPath = std::string(userProfile) + "/Documents/Clone Hero/Songs";

		std::replace(data.currentSongPath.begin(), data.currentSongPath.end(), '\\', '/');
		std::replace(defaultSongsPath.begin(), defaultSongsPath.end(), '\\', '/');
		free(userProfile);
	}

	data.songFolders.push_back(defaultSongsPath);
}

void Config::RenderPathEditor()
{
	char songPathBuf[512];
	strncpy_s(songPathBuf, data.currentSongPath.c_str(), sizeof(songPathBuf) - 1);

	ImGui::TextUnformatted("Current Song File (currentsong.txt)");
	ImGui::SetNextItemWidth(-95.0f);
	if (ImGui::InputText("##currentSongPath", songPathBuf, sizeof(songPathBuf)))
	{
		data.currentSongPath = songPathBuf;
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse...##songfile", ImVec2(85.0f, 0)))
	{
		std::string picked = Window::OpenFileBrowser();
		if (!picked.empty()) { data.currentSongPath = picked; }
	}

	ImGui::Spacing();
	ImGui::TextUnformatted("Song Folders");

	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(data.songFolders.size()); ++i)
	{
		ImGui::PushID(i);
		char folderBuf[512];
		strncpy_s(folderBuf, data.songFolders[i].c_str(), sizeof(folderBuf) - 1);

		ImGui::SetNextItemWidth(-175.0f);
		if (ImGui::InputText("##folder", folderBuf, sizeof(folderBuf)))
		{
			data.songFolders[i] = folderBuf;
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...", ImVec2(85.0f, 0)))
		{
			std::string picked = Window::OpenFolderBrowser();
			if (!picked.empty()) { data.songFolders[i] = picked; }
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove", ImVec2(75.0f, 0)))
		{
			removeIndex = i;
		}
		ImGui::PopID();
	}

	if (removeIndex != -1)
	{
		data.songFolders.erase(data.songFolders.begin() + removeIndex);
	}

	if (ImGui::Button("+ Add Song Folder"))
	{
		std::string picked = Window::OpenFolderBrowser();
		if (!picked.empty())
		{
			data.songFolders.push_back(picked);
		}
	}
}

void Config::OnboardingPopup()
{
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("First Time Setup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Looks like this is your first time launching Hababot2.");
		ImGui::TextDisabled("Verify your Clone Hero paths below before getting started:");
		ImGui::Separator();
		ImGui::Spacing();

		RenderPathEditor();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save & Continue", ImVec2(160, 0)))
		{
			auto& folders = Config::data.songFolders;
			folders.erase(std::remove_if(folders.begin(), folders.end(),
				[](const std::string& s) { return s.empty(); }), folders.end());

			Config::data.setupComplete = true;
			Config::Save();
			SongLibrary::Initialize();
			ImGui::CloseCurrentPopup();
		}

		if (std::filesystem::exists("config.json"))
		{
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)))
			{
				Config::Load();
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
}