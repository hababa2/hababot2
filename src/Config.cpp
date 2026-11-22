#include "Config.hpp"

#include "Window.hpp"
#include "SongLibrary.hpp"

#include "json.hpp"

#include "imgui.h"

#include <fstream>
#include <iostream>

ConfigSettings Config::data{};

bool Config::Load(const std::string& filepath)
{
	std::ifstream file(filepath);
	if (!file.is_open())
	{
		SetDefaults();
		return false;
	}

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

		if (j.contains("currentSongPath")) { data.currentSongPath = j["currentSongPath"]; }

		if (j.contains("songs") && j["songs"].contains("folders"))
		{
			data.songFolders = j["songs"]["folders"].get<std::vector<std::string>>();
		}

		if (j.contains("custom_commands"))
		{
			for (auto& [cmd, response] : j["custom_commands"].items())
			{
				data.customCommands[cmd] = response.get<std::string>();
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "[Config] Failed to parse config: " << e.what() << "\n";
	}

	return true;
}

void Config::Save(const std::string& filepath)
{
	nlohmann::json j;
	j["currentSongPath"] = data.currentSongPath;
	j["songs"]["folders"] = data.songFolders;
	j["custom_commands"] = nlohmann::json::object();

	for (const auto& [cmd, response] : data.customCommands)
	{
		j["custom_commands"][cmd] = response;
	}

	std::ofstream file(filepath);
	if (file.is_open())
	{
		file << j.dump(4);
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

			Config::Save("config.json");
			SongLibrary::Initialize();
			ImGui::CloseCurrentPopup();
		}

		if (std::filesystem::exists("config.json"))
		{
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)))
			{
				Config::Load("config.json");
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
}