#pragma once

#include "Config.hpp"

#include "json.hpp"

#include <iostream>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct ScannedSong
{
	std::string artist;
	std::string name;
	std::string charter;
};

class SongLibrary
{
public:
	static void Initialize();

	static void LoadCache(const fs::path& cachePath = "songs.json");
	static void ScanAsync(const fs::path& cachePath = "songs.json");
	static std::string GetPayload();


private:
	static void RebuildPayloadLocked();
	static std::string StripCloneHeroTags(std::string_view input);

	static json songs;
	static std::mutex mtx;
	static std::string cachedPayload;
	static std::atomic<bool> isScanning;
	static std::vector<ScannedSong> songLibrary;
};