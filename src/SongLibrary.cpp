#include "SongLibrary.hpp"

#include "TwitchBot.hpp"

#include <fstream>
#include <thread>
#include <charconv>

std::mutex SongLibrary::mtx;
json SongLibrary::songs = json::array();
std::string SongLibrary::cachedPayload;
std::atomic<bool> SongLibrary::isScanning{ false };
std::vector<ScannedSong> SongLibrary::songLibrary;

void SongLibrary::Initialize()
{
	LoadCache();
	ScanAsync();
}

void SongLibrary::LoadCache(const fs::path& cachePath)
{
	std::lock_guard<std::mutex> lock(mtx);
	if (fs::exists(cachePath))
	{
		try
		{
			std::ifstream in(cachePath);
			json parsed = json::parse(in);
			if (parsed.is_array())
			{
				songs = std::move(parsed);
				songLibrary.clear();
				songLibrary.reserve(songs.size());

				for (const auto& item : songs)
				{
					ScannedSong s;
					s.artist = item.value("artist", "");
					s.name = item.value("name", "");

					if (IsBlank(s.name) || IsBlank(s.artist)) { continue; }

					s.charter = item.value("charter", "");
					s.genre = item.value("genre", "");
					s.songLengthMs = item.value("length", int64_t{ 0 });
					s.instruments = item.value("instruments", uint16_t{ 0 });

					std::string folderUtf8 = item.value("path", "");
					if (!folderUtf8.empty())
					{
						s.folder = std::filesystem::path(
							reinterpret_cast<const char8_t*>(folderUtf8.data()),
							reinterpret_cast<const char8_t*>(folderUtf8.data() + folderUtf8.size()));
					}

					songLibrary.push_back(std::move(s));
				}
			}
		}
		catch (const std::exception&)
		{
			songs = json::array();
			songLibrary.clear();
		}
	}
	RebuildPayloadLocked();
}

std::filesystem::path SongLibrary::FindSongFolder(std::string_view currentSongRaw)
{
	std::string normRaw = NormalizeAlphaNum(currentSongRaw);
	if (normRaw.empty()) { return {}; }

	std::lock_guard<std::mutex> lock(mtx);
	std::filesystem::path bestMatch;
	size_t bestScore = 0;

	for (const ScannedSong& song : songLibrary)
	{
		std::string normName = NormalizeAlphaNum(song.name);
		if (normName.empty() || normRaw.find(normName) == std::string::npos) { continue; }

		size_t score = normName.size();
		std::string normArtist = NormalizeAlphaNum(song.artist);
		if (!normArtist.empty() && normRaw.find(normArtist) != std::string::npos)
		{
			score += normArtist.size() + 100;
		}

		std::string normCharter = NormalizeAlphaNum(song.charter);
		if (!normCharter.empty() && normRaw.find(normCharter) != std::string::npos)
		{
			score += normCharter.size() + 50;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestMatch = song.folder;
		}
	}

	return bestMatch;
}

std::string SongLibrary::GetPayload()
{
	std::lock_guard<std::mutex> lock(mtx);
	return cachedPayload;
}

std::string SongLibrary::StripCloneHeroTags(std::string_view input)
{
	std::string out;
	out.reserve(input.size());

	bool inTag = false;
	for (size_t i = 0; i < input.size(); ++i)
	{
		if (input[i] == '<') { inTag = true; continue; }
		if (input[i] == '>' && inTag) { inTag = false; continue; }
		if (!inTag) { out.push_back(input[i]); }
	}

	static constexpr std::string_view invisibleSeqs[] = {
		"\xE2\x80\x8B", // U+200B Zero-Width Space
		"\xE2\x80\x8C", // U+200C Zero-Width Non-Joiner
		"\xE2\x80\x8D", // U+200D Zero-Width Joiner
		"\xE2\x80\x8E", // U+200E Left-to-Right Mark
		"\xE2\x80\x8F", // U+200F Right-to-Left Mark
		"\xEF\xBB\xBF", // U+FEFF Zero-Width No-Break Space / BOM
		"\xE2\xA0\x80", // U+2800 Braille Pattern Blank
		"\xE3\x85\xA4"  // U+3164 Hangul Filler
	};

	for (std::string_view seq : invisibleSeqs)
	{
		size_t pos = 0;
		while ((pos = out.find(seq, pos)) != std::string::npos)
		{
			out.erase(pos, seq.size());
		}
	}

	size_t first = out.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) { return ""; }
	size_t last = out.find_last_not_of(" \t\r\n");
	return out.substr(first, (last - first + 1));
}

std::string SongLibrary::NormalizeAlphaNum(std::string_view input)
{
	std::string out;
	out.reserve(input.size());
	for (unsigned char c : input)
	{
		if (c < 128 && std::isalnum(static_cast<unsigned char>(c)))
		{
			out.push_back(static_cast<char>(std::tolower(c)));
		}
	}
	return out;
}

bool SongLibrary::IsBlank(std::string_view sv)
{
	for (unsigned char c : sv)
	{
		if (c > 0x20) { return false; }
	}
	return true;
}

std::string SongLibrary::ToLowerAscii(std::string_view sv)
{
	std::string out;
	out.reserve(sv.size());
	for (unsigned char c : sv)
	{
		if (c >= 'A' && c <= 'Z') { out.push_back(static_cast<char>(c + 32)); }
		else { out.push_back(static_cast<char>(c)); }
	}
	return out;
}

bool SongLibrary::TryParseInt(std::string_view sv, int64_t& out)
{
	if (sv.empty()) { return false; }
	auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
	return ec == std::errc{};
}

void SongLibrary::RebuildPayloadLocked()
{
	std::vector<std::string> artists, genres, charters;
	std::unordered_map<std::string, uint32_t> artistIdx, genreIdx, charterIdx;

	auto intern = [](const std::string& val,
		std::vector<std::string>& table,
		std::unordered_map<std::string, uint32_t>& map) -> uint32_t {
		auto [it, inserted] = map.emplace(val, static_cast<uint32_t>(table.size()));
		if (inserted) { table.push_back(val); }
		return it->second;
	};

	json rows = json::array();
	for (const ScannedSong& s : songLibrary)
	{
		if (IsBlank(s.name) || IsBlank(s.artist)) { continue; }

		uint32_t a = intern(s.artist, artists, artistIdx);
		uint32_t c = intern(s.charter, charters, charterIdx);
		uint32_t g = intern(s.genre, genres, genreIdx);
		int64_t lenSec = (s.songLengthMs > 0) ? (s.songLengthMs / 1000) : 0;

		rows.push_back(json::array({ a, s.name, c, g, lenSec, s.instruments }));
	}

	json msg = {
		{ "action", "song_list" },
		{ "v", 2 },
		{ "scanning", isScanning.load() },
		{ "a", std::move(artists) },
		{ "g", std::move(genres) },
		{ "c", std::move(charters) },
		{ "d", std::move(rows) }
	};

	cachedPayload = msg.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

int64_t SongLibrary::FindSongLengthMs(std::string_view query)
{
	std::string lowerQuery(query);
	std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	size_t first = lowerQuery.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) { return 0; }
	lowerQuery.erase(0, first);
	if (lowerQuery.starts_with("any ")) { return 0; }

	std::string normQuery = NormalizeAlphaNum(query);
	if (normQuery.empty()) { return 0; }

	std::lock_guard<std::mutex> lock(mtx);

	for (const ScannedSong& song : songLibrary)
	{
		if (song.songLengthMs <= 0) { continue; }

		std::string full = song.artist + " - " + song.name;
		std::transform(full.begin(), full.end(), full.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (full == lowerQuery)
		{
			return song.songLengthMs;
		}
	}

	int64_t bestLength = 0;
	size_t bestScore = 0;

	for (const ScannedSong& song : songLibrary)
	{
		if (song.songLengthMs <= 0) { continue; }

		std::string normName = NormalizeAlphaNum(song.name);
		if (normName.empty() || normQuery.find(normName) == std::string::npos) { continue; }

		size_t score = normName.size();
		std::string normArtist = NormalizeAlphaNum(song.artist);
		if (!normArtist.empty() && normQuery.find(normArtist) != std::string::npos)
		{
			score += normArtist.size() + 100;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestLength = song.songLengthMs;
		}
	}

	return bestLength;
}

void SongLibrary::ScanAsync(const fs::path& cachePath)
{
	bool expected = false;
	if (!isScanning.compare_exchange_strong(expected, true)) { return; }

	{
		std::lock_guard<std::mutex> lock(mtx);
		RebuildPayloadLocked();
	}

	std::thread([cachePath]() {
		std::vector<ScannedSong> scanned;

		try
		{
			for (const std::string& folderPath : Config::data.songFolders)
			{
				std::error_code ec;
				std::filesystem::path absPath = std::filesystem::absolute(folderPath, ec);
				if (ec) { absPath = folderPath; }

				std::wstring nativePath = absPath.wstring();
				if (!nativePath.starts_with(L"\\\\?\\"))
				{
					nativePath = L"\\\\?\\" + nativePath;
				}
				std::filesystem::path longRootPath(nativePath);

				if (!std::filesystem::exists(longRootPath, ec))
				{
					std::cerr << "Songs directory not found: " << folderPath << std::endl;
					continue;
				}

				auto options = std::filesystem::directory_options::skip_permission_denied;

				std::filesystem::recursive_directory_iterator it(longRootPath, options, ec);
				std::filesystem::recursive_directory_iterator end;

				while (it != end)
				{
					if (ec)
					{
						ec.clear();
						it.increment(ec);
						continue;
					}

					bool isFile = it->is_regular_file(ec);
					if (!ec && isFile)
					{
						std::wstring filename = it->path().filename().wstring();
						std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);

						if (filename == L"song.ini")
						{
							std::ifstream file(it->path());
							if (file.is_open())
							{
								ScannedSong song;
								std::string line;

								while (std::getline(file, line))
								{
									if (line.starts_with("\xEF\xBB\xBF"))
									{
										line.erase(0, 3);
									}

									size_t eq = line.find('=');
									if (eq == std::string::npos) { continue; }

									std::string key = StripCloneHeroTags(std::string_view(line).substr(0, eq));
									std::transform(key.begin(), key.end(), key.begin(),
										[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

									std::string val = StripCloneHeroTags(std::string_view(line).substr(eq + 1));

									if (key == "name") { song.name = std::move(val); }
									else if (key == "artist") { song.artist = std::move(val); }
									else if (key == "charter" || (key == "frets" && song.charter.empty()))
									{
										song.charter = std::move(val);
									}
									else if (key == "genre") { song.genre = std::move(val); }
									else if (key == "song_length")
									{
										int64_t ms = 0;
										if (TryParseInt(val, ms) && ms > 0) { song.songLengthMs = ms; }
									}
									else if (key.starts_with("diff_"))
									{
										int64_t diff = -1;
										if (TryParseInt(val, diff) && diff >= 0)
										{
											if (key == "diff_guitar") { song.instruments |= InstrumentGuitar; }
											else if (key == "diff_bass") { song.instruments |= InstrumentBass; }
											else if (key == "diff_rhythm") { song.instruments |= InstrumentRhythm; }
											else if (key == "diff_drums") { song.instruments |= InstrumentDrums; }
											else if (key == "diff_drums_real") { song.instruments |= (InstrumentDrums); }
											else if (key == "diff_keys") { song.instruments |= InstrumentKeys; }
										}
									}
								}

								if (!IsBlank(song.name) && !IsBlank(song.artist))
								{
									song.folder = it->path().parent_path();
									scanned.push_back(std::move(song));
								}
							}
						}
					}
					else if (it->is_directory(ec) && !ec)
					{
						std::wstring dirName = it->path().filename().wstring();
						if (dirName == L".synchotic" || dirName == L".dm-sync")
						{
							it.disable_recursion_pending();
							ec.clear();
							it.increment(ec);
							continue;
						}
					}

					ec.clear();
					it.increment(ec);
					if (ec)
					{
						it.disable_recursion_pending();
						ec.clear();
						it.increment(ec);
					}
				}
			}

			std::sort(scanned.begin(), scanned.end(), [](const ScannedSong& a, const ScannedSong& b) {
				if (a.artist != b.artist) { return a.artist < b.artist; }
				return a.name < b.name;
			});

			std::unordered_map<std::string, size_t> seen;
			seen.reserve(scanned.size());

			std::vector<ScannedSong> deduped;
			deduped.reserve(scanned.size());

			for (ScannedSong& s : scanned)
			{
				std::string key = ToLowerAscii(s.artist) + '\x1F' + ToLowerAscii(s.name) + '\x1F' + ToLowerAscii(s.charter);

				auto [it, inserted] = seen.emplace(std::move(key), deduped.size());
				if (inserted)
				{
					deduped.push_back(std::move(s));
				}
				else
				{
					ScannedSong& existing = deduped[it->second];
					existing.instruments |= s.instruments;
					if (existing.songLengthMs <= 0 && s.songLengthMs > 0) { existing.songLengthMs = s.songLengthMs; }
					if (existing.genre.empty() && !s.genre.empty()) { existing.genre = std::move(s.genre); }
					if (existing.charter.empty() && !s.charter.empty()) { existing.charter = std::move(s.charter); }
				}
			}

			scanned = std::move(deduped);

			std::sort(scanned.begin(), scanned.end(), [](const ScannedSong& a, const ScannedSong& b) {
				std::string aArt = ToLowerAscii(a.artist);
				std::string bArt = ToLowerAscii(b.artist);
				if (aArt != bArt) { return aArt < bArt; }
				return ToLowerAscii(a.name) < ToLowerAscii(b.name);
			});

			nlohmann::json jArray = nlohmann::json::array();
			for (const ScannedSong& s : scanned)
			{
				auto u8 = s.folder.u8string();
				std::string folderUtf8(reinterpret_cast<const char*>(u8.data()), u8.size());

				jArray.push_back({
					{ "artist", s.artist },
					{ "name", s.name },
					{ "charter", s.charter },
					{ "genre", s.genre },
					{ "length", s.songLengthMs },
					{ "instruments", s.instruments },
					{ "path", std::move(folderUtf8) }
				});
			}

			std::string newPayload;
			bool changed = false;

			{
				std::lock_guard<std::mutex> lock(mtx);
				changed = (jArray != songs);
				songs = std::move(jArray);
				songLibrary = std::move(scanned);
				isScanning.store(false);
				RebuildPayloadLocked();
				newPayload = cachedPayload;
			}

			if (changed)
			{
				fs::path tmpPath = cachePath;
				tmpPath += ".tmp";
				std::ofstream out(tmpPath);
				if (out)
				{
					out << songs.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
					out.close();
					std::error_code ec;
					fs::rename(tmpPath, cachePath, ec);
				}
			}

			TwitchBot::SendToRelay(newPayload);
		}
		catch (const std::exception& e)
		{
			std::ofstream log("hababot2_crash.log", std::ios::app);
			log << "[Crash] " << e.what() << "\n";
			isScanning.store(false);
		}
	}).detach();
}