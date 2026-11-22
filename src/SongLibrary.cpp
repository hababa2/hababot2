#include "SongLibrary.hpp"

#include "TwitchBot.hpp"

#include <fstream>
#include <thread>

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
            }
        }
        catch (const std::exception&)
        {
            songs = json::array();
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
		if (c < 128 && std::isalnum(c))
		{
			out.push_back(static_cast<char>(std::tolower(c)));
		}
	}
	return out;
}

void SongLibrary::RebuildPayloadLocked()
{
    json msg = {
        { "action", "song_list" },
        { "data", songs },
        { "scanning", isScanning.load() }
    };
    cachedPayload = msg.dump();
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
				return;
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
									[](unsigned char c) { return std::tolower(c); });

								std::string val = StripCloneHeroTags(std::string_view(line).substr(eq + 1));

								if (key == "name") { song.name = std::move(val); }
								else if (key == "artist") { song.artist = std::move(val); }
								else if (key == "charter" || (key == "frets" && song.charter.empty()))
								{
									song.charter = std::move(val);
								}
							}

							if (!song.name.empty() && !song.artist.empty())
							{
								song.folder = it->path().parent_path();
								scanned.push_back(std::move(song));
							}
						}
					}
				}
				else if (it->is_directory() && (it->path().filename() == ".synchotic" || it->path().filename() == ".dm-sync"))
				{
					it.disable_recursion_pending();
					ec.clear();
					it.increment(ec);
					continue;
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

		scanned.erase(std::unique(scanned.begin(), scanned.end(), [](const ScannedSong& a, const ScannedSong& b) {
			return a.artist == b.artist && a.name == b.name && a.charter == b.charter;
		}), scanned.end());

		nlohmann::json jArray = nlohmann::json::array();
		for (const ScannedSong& s : scanned)
		{
			jArray.push_back({
				{ "artist", s.artist },
				{ "name", s.name },
				{ "charter", s.charter },
				{ "path", s.folder }
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
	}).detach();
}