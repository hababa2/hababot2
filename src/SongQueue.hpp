#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <queue>
#include <string>
#include <filesystem>

#undef SendMessage

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace http = beast::http;

using asio::awaitable;
using asio::use_awaitable;

struct SongRequest
{
	std::string request;
	std::string requester;
};

class SongQueue
{
public:
	static void RenderSongQueue();

	static void UpdateCurrentSong();
	static bool DoesSongMatchRequest(std::string_view currentSongRaw, std::string_view requestQuery);

	static std::string SerializeQueue();

	static void AddToQueue(const SongRequest& request);

	static size_t QueueSize();
	static bool RequestsEnabled();

private:
	static std::mutex mtx;

	static std::deque<SongRequest> songQueue;
	static std::string currentSongDisplay;
	static std::string currentSongRequester;
	static std::filesystem::file_time_type lastSongWriteTime;
	static std::chrono::steady_clock::time_point lastSongPollTime;

	static bool requestsEnabled;
	static bool queueVisible;

	static HANDLE JobHandle;
	static HANDLE ProcessHandle;

	friend class TwitchBot;
};