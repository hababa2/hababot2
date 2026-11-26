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

enum InstrumentFlags : uint16_t
{
	InstrumentNone = 0,
	InstrumentGuitar = 1 << 0,
	InstrumentBass = 1 << 1,
	InstrumentRhythm = 1 << 2,
	InstrumentDrums = 1 << 3,
	InstrumentKeys = 1 << 4,
};

struct ScannedSong
{
	std::string artist;
	std::string name;
	std::string charter;
	std::string genre;
	int64_t songLengthMs = 0;
	uint16_t instruments = InstrumentNone;
	std::filesystem::path folder;
};

class SongLibrary
{
public:
	static void Initialize();

	static void LoadCache(const fs::path& cachePath = "songs.json");
	static void ScanAsync(const fs::path& cachePath = "songs.json");
	static std::filesystem::path FindSongFolder(std::string_view currentSongRaw);
	static std::string StripParentheticals(std::string_view input);
	static std::vector<std::string> TokenizeWords(std::string_view input);
	static int64_t FindSongLengthMs(std::string_view query);
	static std::string GetPayload();
	static bool IsScanning();

	static void StartWatcher();
	static void StopWatcher();

private:
	static void RebuildPayloadLocked();
	static std::string StripCloneHeroTags(std::string_view input);
	static std::string NormalizeAlphaNum(std::string_view input);
	static bool IsBlank(std::string_view sv);
	static std::string ToLowerAscii(std::string_view sv);
	static bool TryParseInt(std::string_view sv, int64_t& out);

	static json songs;
	static std::mutex mtx;
	static std::string cachedPayload;
	static std::atomic<bool> isScanning;
	static std::vector<ScannedSong> songLibrary;
};