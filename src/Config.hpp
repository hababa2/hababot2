#pragma once

#include "Commands.hpp"

#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

struct WindowConfig
{
	int left = 100;
	int top = 100;
	int right = 1100;
	int bottom = 800;
	int showCmd = 1;
	bool hasSavedPlacement = false;
};

struct EarlyBirdStats
{
	int first = 0;
	int second = 0;
	int third = 0;
};

struct PersistedSongRequest
{
	std::string request;
	std::string requester;
	int64_t lengthMs = 0;
};

struct QueueConfig
{
	bool requestsEnabled = true;
	bool queueVisible = true;
	std::vector<PersistedSongRequest> items;
};

struct CustomCommand
{
	std::string response;
	CommandPermission permission = CommandPermission::Everyone;
};

struct ConfigSettings
{
	bool setupComplete = false;
	std::string currentSongPath;
	std::vector<std::string> songFolders;
	std::unordered_map<std::string, CustomCommand> customCommands;

	WindowConfig window;
	std::string oauthAccessToken;
	std::unordered_map<std::string, EarlyBirdStats> earlyBirds;
	QueueConfig queue;
};

class Config
{
public:
	static bool Load(const std::string& filepath = "config.json");
	static void Save(const std::string& filepath = "config.json");
	static void SetDefaults();

	static ConfigSettings data;

private:
	static void RenderPathEditor();
	static void OnboardingPopup();

	static std::mutex mtx;

	friend class TwitchBot;
};