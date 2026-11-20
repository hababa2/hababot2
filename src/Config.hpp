#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct ConfigSettings
{
	std::string currentSongPath = "";
	std::vector<std::string> songFolders;
	std::unordered_map<std::string, std::string> customCommands = { { "!ping", "Pong! {user}" } };
};

class Config
{
public:
	static ConfigSettings data;
	static bool Load(const std::string& filepath = "config.json");
	static void Save(const std::string& filepath = "config.json");
	static void SetDefaults();

	static void OnboardingPopup();
};