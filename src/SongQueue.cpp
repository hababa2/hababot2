#include "SongQueue.hpp"

#include "TwitchBot.hpp"
#include "Config.hpp"

#include "imgui.h"

#include "json.hpp"

#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>

std::mutex SongQueue::mtx;

std::deque<SongRequest> SongQueue::songQueue;
std::string SongQueue::currentSongDisplay;
std::string SongQueue::currentSongRequester;
std::filesystem::file_time_type SongQueue::lastSongWriteTime{};
std::chrono::steady_clock::time_point SongQueue::lastSongPollTime{};

bool SongQueue::requestsEnabled = true;
bool SongQueue::queueVisible = true;

HANDLE SongQueue::JobHandle = nullptr;
HANDLE SongQueue::ProcessHandle = nullptr;

void SongQueue::RenderSongQueue()
{
	ImGui::BeginChild("SongQueue", ImVec2(0, 0), ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoScrollbar);

	ImGui::PushFont(TwitchBot::boldFont);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.95f, 0.55f, 1.0f));
	ImGui::TextUnformatted("Now Playing");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	if (!currentSongDisplay.empty())
	{
		ImGui::PushFont(TwitchBot::font);
		ImGui::TextWrapped("%s", currentSongDisplay.c_str());
		ImGui::PopFont();

		if (!currentSongRequester.empty())
		{
			ImGui::PushFont(TwitchBot::headerFont);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
			ImGui::TextWrapped("Requested by %s", currentSongRequester.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}
	}
	else
	{
		ImGui::PushFont(TwitchBot::headerFont);
		ImGui::TextDisabled("In Menus / Idle");
		ImGui::PopFont();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (queueVisible)
	{
		ImGui::PushFont(TwitchBot::boldFont);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.70f, 1.0f, 1.0f));
		ImGui::Text("Song Queue (%zu)", songQueue.size());
		ImGui::PopStyleColor();
		ImGui::PopFont();

		ImGui::PushFont(TwitchBot::headerFont);
		if (songQueue.empty())
		{
			ImGui::TextDisabled("Queue is empty.\nType !sr <Artist - Song> in chat!");
		}
		else
		{
			for (size_t i = 0; i < songQueue.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::SmallButton("X"))
				{
					songQueue.erase(songQueue.begin() + i);
					ImGui::PopID();

					nlohmann::json updatePayload = {
						{ "action", "queue_update" },
						{ "data", nlohmann::json::parse(SerializeQueue()) }
					};
					TwitchBot::SendToRelay(updatePayload.dump());

					break;
				}
				ImGui::SameLine();
				ImGui::TextWrapped("%zu. %s (%s)", i + 1, songQueue[i].request.c_str(), songQueue[i].requester.c_str());
				ImGui::PopID();
			}
		}
		ImGui::PopFont();
	}

	float bottomHeight = 147.0f;
	float availY = ImGui::GetContentRegionAvail().y;
	if (availY > bottomHeight)
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + availY - bottomHeight);
	}

	ImGui::BeginChild("Queue Controls", ImVec2(0, bottomHeight), true, ImGuiWindowFlags_NoScrollbar);
	ImGui::PushFont(TwitchBot::font);

	if (ImGui::Checkbox("Enable Requests", &requestsEnabled))
	{
		nlohmann::json statePayload = {
			{ "action", "queue_state" },
			{ "requests_enabled", requestsEnabled },
			{ "queue_visible", queueVisible },
		};
		TwitchBot::SendToRelay(statePayload.dump());
	}

	if (ImGui::Checkbox("Show Queue", &queueVisible))
	{
		nlohmann::json statePayload = {
			{ "action", "queue_state" },
			{ "requests_enabled", requestsEnabled },
			{ "queue_visible", queueVisible },
		};
		TwitchBot::SendToRelay(statePayload.dump());
	}

	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 50, 50, 255));
	if (ImGui::Button("Clear Queue", ImVec2(-1, 30)))
	{
		songQueue.clear();

		nlohmann::json clearPayload = {
			{ "action", "queue_update" },
			{ "data", nlohmann::json::parse(SerializeQueue()) }
		};
		TwitchBot::SendToRelay(clearPayload.dump());
	}
	ImGui::PopStyleColor();

	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(201, 160, 220, 255));
	if (ImGui::Button("Customize Commands", ImVec2(-1, 30)))
	{
		TwitchBot::showCommandManager = !TwitchBot::showCommandManager;
	}
	ImGui::PopStyleColor();

	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::EndChild();
}

void SongQueue::UpdateCurrentSong()
{
	auto now = std::chrono::steady_clock::now();

	if (now - lastSongPollTime < std::chrono::milliseconds(1000)) { return; }
	lastSongPollTime = now;

	std::error_code ec;

	if (!std::filesystem::exists(Config::data.currentSongPath, ec)) { return; }

	auto writeTime = std::filesystem::last_write_time(Config::data.currentSongPath, ec);
	if (ec || writeTime == lastSongWriteTime) { return; }
	lastSongWriteTime = writeTime;

	std::ifstream file(Config::data.currentSongPath);
	if (!file.is_open()) { return; }

	std::string rawContents;
	std::string formattedDisplay;
	std::string line;

	while (std::getline(file, line))
	{
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		{
			line.pop_back();
		}
		if (line.empty()) { continue; }

		rawContents += line + " ";

		if (!formattedDisplay.empty()) { formattedDisplay += " - "; }
		formattedDisplay += line;
	}

	if (formattedDisplay.empty())
	{
		currentSongDisplay.clear();
		currentSongRequester.clear();
		return;
	}

	if (formattedDisplay != currentSongDisplay)
	{
		currentSongDisplay = formattedDisplay;
		currentSongRequester.clear();

		for (auto it = songQueue.begin(); it != songQueue.end(); ++it)
		{
			if (DoesSongMatchRequest(rawContents, it->request))
			{
				currentSongRequester = it->requester;
				songQueue.erase(it);

				nlohmann::json updatePayload = {
					{ "action", "queue_update" },
					{ "data", nlohmann::json::parse(SerializeQueue()) }
				};
				TwitchBot::SendToRelay(updatePayload.dump());

				break;
			}
		}
	}
}

bool SongQueue::DoesSongMatchRequest(std::string_view currentSongRaw, std::string_view requestQuery)
{
	auto tokenize = [](std::string_view input) {
		std::vector<std::string> tokens;
		std::string current;

		for (unsigned char c : input)
		{
			if (c < 128 && std::isalnum(c))
			{
				current.push_back(static_cast<char>(std::tolower(c)));
			}
			else if (!current.empty())
			{
				tokens.push_back(std::move(current));
				current.clear();
			}
		}

		if (!current.empty()) { tokens.push_back(std::move(current)); }
		return tokens;
	};

	std::vector<std::string> songTokens = tokenize(currentSongRaw);
	std::vector<std::string> rawQueryTokens = tokenize(requestQuery);

	if (songTokens.empty() || rawQueryTokens.empty()) { return false; }

	static constexpr std::array<std::string_view, 51> fillerWords = {
		"play", "some", "something", "anything", "any", "by", "from",
		"the", "a", "an", "of", "on", "in", "to", "for", "with", "and",
		"song", "songs", "track", "tune", "chart", "drums", "drum",
		"clone", "hero", "ch", "pls", "plz", "please", "can", "you",
		"do", "we", "get", "hear", "have", "now", "want",
		"kappa", "lul", "pog", "pogchamp", "catjam", "peped", "kekw",
		"monkas", "sadge", "pepega", "ez", "clap"
	};

	auto isFiller = [&](std::string_view word) {
		return std::find(fillerWords.begin(), fillerWords.end(), word) != fillerWords.end();
	};

	std::vector<std::string> queryTokens;
	for (const std::string& token : rawQueryTokens)
	{
		if (!isFiller(token)) { queryTokens.push_back(token); }
	}

	if (queryTokens.empty()) { queryTokens = std::move(rawQueryTokens); }

	std::string fullAcronym;
	std::string strictAcronym;
	for (const std::string& token : songTokens)
	{
		if (!token.empty())
		{
			fullAcronym.push_back(token[0]);
			if (!isFiller(token)) { strictAcronym.push_back(token[0]); }
		}
	}

	auto levenshtein = [](std::string_view s1, std::string_view s2) {
		size_t len1 = s1.size();
		size_t len2 = s2.size();

		if (len1 == 0) { return len2; }
		if (len2 == 0) { return len1; }

		std::vector<size_t> col(len2 + 1);
		std::vector<size_t> prevCol(len2 + 1);

		for (size_t i = 0; i <= len2; ++i) { prevCol[i] = i; }
		for (size_t i = 0; i < len1; ++i)
		{
			col[0] = i + 1;
			for (size_t j = 0; j < len2; ++j)
			{
				col[j + 1] = std::min({
					prevCol[j + 1] + 1,
					col[j] + 1,
					prevCol[j] + (s1[i] != s2[j])
					});
			}

			col.swap(prevCol);
		}

		return prevCol[len2];
	};

	int matchedCount = 0;
	for (const std::string& qToken : queryTokens)
	{
		bool tokenMatched = false;

		for (const std::string& sToken : songTokens)
		{
			if (qToken == sToken ||
				(qToken.length() >= 5 && sToken.find(qToken) != std::string::npos) ||
				(sToken.length() >= 5 && qToken.find(sToken) != std::string::npos))
			{
				tokenMatched = true;
				break;
			}

			if (qToken.length() >= 4 && sToken.length() >= 4)
			{
				size_t dist = levenshtein(qToken, sToken);
				if (dist <= 1 || (qToken.length() >= 7 && dist <= 2))
				{
					tokenMatched = true;
					break;
				}
			}
		}

		if (!tokenMatched && qToken.length() >= 3)
		{
			if (fullAcronym.find(qToken) != std::string::npos ||
				strictAcronym.find(qToken) != std::string::npos)
			{
				tokenMatched = true;
			}
		}

		if (tokenMatched) { ++matchedCount; }
	}

	size_t requiredMatches = queryTokens.size();
	if (queryTokens.size() > 3) { requiredMatches = queryTokens.size() - 1; }
	if (queryTokens.size() > 6) { requiredMatches = queryTokens.size() - 2; }

	return matchedCount >= requiredMatches;
}

std::string SongQueue::SerializeQueue()
{
	nlohmann::json queueJson = nlohmann::json::array();

	std::lock_guard<std::mutex> lock(mtx);

	for (const auto& song : songQueue)
	{
		queueJson.push_back({
			{ "query", song.request },
			{ "requester", song.requester }
			});
	}

	return queueJson.dump();
}

void SongQueue::AddToQueue(const SongRequest& request)
{
	int activeRequests = 0;
	for (const SongRequest& existing : SongQueue::songQueue)
	{
		if (existing.requester == request.requester) { ++activeRequests; }
	}

	if (activeRequests >= 2)
	{
		TwitchBot::SendMessage("You already have 2 songs in the queue! Wait for one to play, @" + request.requester);
		return;
	}

	for (const SongRequest& existing : SongQueue::songQueue)
	{
		std::string lowerExisting = existing.request;
		std::string lowerNew = request.request;
		std::transform(lowerExisting.begin(), lowerExisting.end(), lowerExisting.begin(), ::tolower);
		std::transform(lowerNew.begin(), lowerNew.end(), lowerNew.begin(), ::tolower);

		if (lowerExisting == lowerNew)
		{
			TwitchBot::SendMessage("\"" + existing.request + "\" is already in the queue, @" + request.requester);
			return;
		}
	}

	SongQueue::songQueue.push_back(request);
	TwitchBot::SendMessage("Added \"" + request.request + "\" to the queue (#" + std::to_string(SongQueue::QueueSize()) + "), @" + request.requester);

	nlohmann::json updatePayload = {
		{ "action", "queue_update" },
		{ "data", nlohmann::json::parse(SongQueue::SerializeQueue()) }
	};
	TwitchBot::SendToRelay(updatePayload.dump());
}

size_t SongQueue::QueueSize()
{
	return songQueue.size();
}

bool SongQueue::RequestsEnabled()
{
	return requestsEnabled;
}