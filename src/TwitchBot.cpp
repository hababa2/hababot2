#include "TwitchBot.hpp"

#include "OAuth.hpp"
#include "Badges.hpp"
#include "Emotes.hpp"
#include "Events.hpp"
#include "Config.hpp"
#include "SongQueue.hpp"
#include "SongLibrary.hpp"
#include "Commands.hpp"
#include "Window.hpp"
#include "Debug.hpp"

#include "json.hpp"

#include <cpr/cpr.h>

#include <d3d11.h>

#include <iostream>
#include <algorithm>
#include <fstream>

bool TwitchBot::isRunning = true;
bool TwitchBot::showSettings = false;
bool TwitchBot::showOnboarding = false;
bool TwitchBot::showCommandManager = false;
bool TwitchBot::relayWriteInProgress = false;
std::atomic<bool> TwitchBot::relayConnected{ false };
std::mutex TwitchBot::mtx;
TwitchUser TwitchBot::bot;

ImFont* TwitchBot::font;
ImFont* TwitchBot::boldFont;
ImFont* TwitchBot::italicFont;
ImFont* TwitchBot::headerFont;

asio::io_context* TwitchBot::ioc;
websocket::stream<beast::ssl_stream<asio::ip::tcp::socket>>* TwitchBot::ws;
websocket::stream<beast::ssl_stream<asio::ip::tcp::socket>>* TwitchBot::relayWs = nullptr;
std::queue<std::string> TwitchBot::relayWriteQueue;

std::unordered_map<std::string, std::vector<CheermoteTier>> TwitchBot::cheermotes;
std::unordered_set<std::string> TwitchBot::knownChatters;
std::unordered_set<std::string> TwitchBot::confirmedRaiders;
ThreadSafeQueue<ChatMessage> TwitchBot::chatQueue;
std::queue<std::string> TwitchBot::writeQueue;
std::deque<ChatMessage> TwitchBot::chatHistory;
RaidTrackerState TwitchBot::activeRaid;
HypeTrainState TwitchBot::hypeTrain;

int TwitchBot::earlyBirdCount = 0;
std::unordered_map<std::string, EarlyBirdStats> TwitchBot::earlyBirdLeaderboard;

void TwitchBot::Initialize(asio::io_context& ioc_, asio::ssl::context& ctx_)
{
	ioc = &ioc_;
	ws = new websocket::stream<beast::ssl_stream<asio::ip::tcp::socket>>(ioc_, ctx_);

	showOnboarding = !Config::Load("config.json");
	SongQueue::LoadDefaultAlbumArt("default_album.png");
	SongLibrary::Initialize();
	LoadEarlyBirdStats();

	asio::co_spawn(*ioc, ConnectToRelay(), asio::detached);

	Commands::RegisterCommands();
}

asio::awaitable<void> TwitchBot::ConnectToRelay()
{
	try
	{
		asio::ssl::context ctx(asio::ssl::context::tlsv12_client);
		ctx.set_default_verify_paths();

		relayWs = new websocket::stream<beast::ssl_stream<asio::ip::tcp::socket>>(*ioc, ctx);

		asio::ip::tcp::resolver resolver(*ioc);
		auto const results = co_await resolver.async_resolve("songs.hababot2.com", "443", use_awaitable);

		co_await asio::async_connect(beast::get_lowest_layer(*relayWs), results, use_awaitable);

		if (!SSL_set_tlsext_host_name(relayWs->next_layer().native_handle(), "songs.hababot2.com"))
		{
			throw std::runtime_error("Failed to set SNI Hostname for relay");
		}

		relayWs->set_option(boost::beast::websocket::stream_base::decorator(
			[](boost::beast::websocket::request_type& req)
		{
			req.set(boost::beast::http::field::user_agent, "Hababot-Desktop-Client/1.0");
		}
		));

		boost::beast::websocket::stream_base::timeout opt{
			std::chrono::seconds(30),
			std::chrono::seconds(45),
			true
		};
		relayWs->set_option(opt);

		co_await relayWs->next_layer().async_handshake(asio::ssl::stream_base::client, use_awaitable);
		co_await relayWs->async_handshake("songs.hababot2.com", "/ws-relay", use_awaitable);
		relayWs->text(true);

		nlohmann::json auth = {
			{ "role", "streamer" },
			{ "channel", OAuth::ChannelName() }
		};
		co_await relayWs->async_write(asio::buffer(auth.dump()), use_awaitable);
		relayConnected = true;
		std::cout << "[Relay] Connected to central hub as " << OAuth::ChannelName() << "\n";

		SendToRelay(SongLibrary::GetPayload());

		beast::flat_buffer buffer;
		while (true)
		{
			co_await relayWs->async_read(buffer, use_awaitable);
			std::string payload = beast::buffers_to_string(buffer.data());
			buffer.consume(buffer.size());

			std::cout << "[Relay] Received: " << payload << "\n";

			auto j = nlohmann::json::parse(payload, nullptr, false);
			if (j.is_discarded()) { continue; }

			std::string action = j.value("action", "");

			try
			{
				if (action == "get_songs")
				{
					SendToRelay(SongLibrary::GetPayload());

					nlohmann::json updatePayload = {
						{ "action", "queue_update" },
						{ "data", nlohmann::json::parse(SongQueue::SerializeQueue()) }
					};
					SendToRelay(updatePayload.dump());
				}
				else if (action == "request_song")
				{
					if (!SongQueue::requestsEnabled)
					{
						std::cout << "[BOT] Request ignored: Queue is currently closed.\n";
						continue;
					}

					std::string query = j.value("query", "");
					std::string requester = j.value("requester", "Anonymous");

					bool isDuplicate = false;
					bool limitReached = false;
					size_t queuePos = 0;

					{
						std::lock_guard<std::mutex> lock(mtx);
						int activeRequests = 0;

						for (const auto& existing : SongQueue::songQueue)
						{
							if (existing.requester == requester && requester != "Anonymous")
							{
								++activeRequests;
							}
							if (existing.request == query)
							{
								isDuplicate = true;
							}
						}

						if (activeRequests >= 2)
						{
							limitReached = true;
						}
						else if (!isDuplicate)
						{
							SongQueue::songQueue.push_back(SongRequest{ query, requester });
							queuePos = SongQueue::songQueue.size();
						}
					}

					if (limitReached)
					{
						SendToRelay(nlohmann::json{
							{ "action", "request_failed" },
							{ "requester", requester },
							{ "error", "You already have 2 songs in the queue!" }
							}.dump());
					}
					else if (isDuplicate)
					{
						SendToRelay(nlohmann::json{
							{ "action", "request_failed" },
							{ "requester", requester },
							{ "error", "Song is already in the queue!" }
							}.dump());
					}
					else
					{
						SendMessage("Added \"" + query + "\" to the queue (#" + std::to_string(queuePos) + ") requested by " + requester + "!");

						nlohmann::json updatePayload = {
							{ "action", "queue_update" },
							{ "data", nlohmann::json::parse(SongQueue::SerializeQueue()) }
						};
						SendToRelay(updatePayload.dump());
					}
				}
			}
			catch (const std::exception& e)
			{
				std::ofstream errLog("hababot_error.log", std::ios::app);
				errLog << "[WS Handler Error] " << e.what() << "\n";
			}
		}
	}
	catch (std::exception const& e)
	{
		relayConnected = false;
		std::cerr << "[Relay] Connection dropped: " << e.what() << "\n";
	}
}

void TwitchBot::SendToRelay(const std::string& msg)
{
	if (!relayWs || !relayConnected.load()) { return; }

	asio::post(*ioc, [msg]() mutable {
		if (!relayWs || !relayConnected.load()) { return; }

		relayWriteQueue.push(std::move(msg));

		if (!relayWriteInProgress)
		{
			relayWriteInProgress = true;
			asio::co_spawn(*ioc, RelayWriteLoop(), asio::detached);
		}
	});
}

asio::awaitable<void> TwitchBot::RelayWriteLoop()
{
	try
	{
		while (!relayWriteQueue.empty())
		{
			co_await relayWs->async_write(asio::buffer(relayWriteQueue.front()), use_awaitable);
			relayWriteQueue.pop();
		}
	}
	catch (const std::exception& e)
	{
		std::ofstream errLog("hababot2_error.log", std::ios::app);
		errLog << "[RelayWriteLoop Error] " << e.what() << "\n";

		while (!relayWriteQueue.empty()) { relayWriteQueue.pop(); }
		relayConnected = false;
	}

	relayWriteInProgress = false;
}

void TwitchBot::FetchCheermotes()
{
	cpr::Response r = cpr::Get(
		cpr::Url{ "https://api.twitch.tv/helix/bits/cheermotes" },
		cpr::Header{
			{ "Authorization", "Bearer " + OAuth::AccessToken() },
			{ "Client-Id", OAuth::ClientId() }
		}
	);

	if (r.status_code != 200) { return; }

	auto json = nlohmann::json::parse(r.text, nullptr, false);
	if (json.is_discarded() || !json.contains("data")) { return; }

	for (const auto& item : json["data"])
	{
		std::string prefix = item.value("prefix", "");
		if (prefix.empty() || !item.contains("tiers")) { continue; }

		std::transform(prefix.begin(), prefix.end(), prefix.begin(),
			[](unsigned char c) { return std::tolower(c); });

		std::vector<CheermoteTier> tiers;
		for (const auto& tierJson : item["tiers"])
		{
			CheermoteTier tier;
			tier.minBits = tierJson.value("min_bits", 0);
			tier.color = HexToImvec4(tierJson.value("color", "#FFFFFF"));

			if (tierJson.contains("images") &&
				tierJson["images"].contains("dark") &&
				tierJson["images"]["dark"].contains("animated") &&
				tierJson["images"]["dark"]["animated"].contains("2"))
			{
				tier.url = tierJson["images"]["dark"]["animated"]["2"].get<std::string>();
			}

			if (!tier.url.empty())
			{
				tiers.push_back(std::move(tier));
			}
		}

		std::sort(tiers.begin(), tiers.end(), [](const CheermoteTier& a, const CheermoteTier& b) {
			return a.minBits > b.minBits;
		});

		cheermotes[prefix] = std::move(tiers);
	}
}

awaitable<void> TwitchBot::Run()
{
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 3;
	fontConfig.OversampleV = 1;

	ImGuiIO& io = ImGui::GetIO();

	font = io.Fonts->AddFontFromFileTTF("Roobert.otf", 22.0f, &fontConfig);
	boldFont = io.Fonts->AddFontFromFileTTF("RoobertBold.otf", 22.0f, &fontConfig);
	italicFont = io.Fonts->AddFontFromFileTTF("RoobertItalic.otf", 22.0f, &fontConfig);
	headerFont = io.Fonts->AddFontFromFileTTF("RoobertBold.otf", 18.0f, &fontConfig);
	io.Fonts->Build();

	ImGuiStyle& style = ImGui::GetStyle();
	style.ItemSpacing = ImVec2(2.0f, 5.0f);

	try
	{
		if (!OAuth::Authenticate())
		{
			throw std::runtime_error("Failed to authenticate");
		}

		FetchCheermotes();

		asio::ip::tcp::resolver resolver(*ioc);

		auto const results = co_await resolver.async_resolve("irc-ws.chat.twitch.tv", "443", use_awaitable);
		co_await asio::async_connect(beast::get_lowest_layer(*ws), results, use_awaitable);

		if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), "irc-ws.chat.twitch.tv"))
		{
			throw std::runtime_error("Failed to set SNI Hostname");
		}

		co_await ws->next_layer().async_handshake(asio::ssl::stream_base::client, use_awaitable);
		co_await ws->async_handshake("irc-ws.chat.twitch.tv:443", "/", use_awaitable);

		ws->text(true);

		std::string capReq = "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n";
		co_await ws->async_write(asio::buffer(capReq), use_awaitable);

		std::string passCmd = "PASS oauth:" + OAuth::AccessToken() + "\r\n";
		co_await ws->async_write(asio::buffer(passCmd), use_awaitable);

		std::string nickCmd = "NICK " + OAuth::ChannelName() + "\r\n";
		co_await ws->async_write(asio::buffer(nickCmd), use_awaitable);

		std::string joinCmd = "JOIN #" + OAuth::ChannelName() + "\r\n";
		co_await ws->async_write(asio::buffer(joinCmd), use_awaitable);

		beast::flat_buffer buffer;
		while (true)
		{
			co_await ws->async_read(buffer, use_awaitable);
			std::string_view msg(static_cast<const char*>(buffer.data().data()), buffer.size());

			if (msg.starts_with("PING"))
			{
				asio::post(*ioc, []() {
					bool writeInProgress = !writeQueue.empty();
					writeQueue.push("PONG :tmi.twitch.tv\r\n");
					if (!writeInProgress) { asio::co_spawn(*ioc, WriteLoop(), asio::detached); }
				});
			}
			else if (msg.find("PRIVMSG") != std::string_view::npos)
			{
				if (std::optional<ChatMessage> parsed = ParseMsg(msg))
				{
					chatQueue.Push(std::move(parsed.value()));
				}
			}
			else if (msg.find("USERSTATE") != std::string_view::npos || msg.find("GLOBALUSERSTATE") != std::string_view::npos)
			{
				std::lock_guard<std::mutex> lock(mtx);

				if (msg.find("display-name=" + OAuth::ChannelName()))
				{
					ParseState(msg, bot);
				}
			}
			else if (msg.find("CLEARMSG") != std::string_view::npos)
			{
				ChatMessage deleteCmd;
				deleteCmd.action = ChatQueueAction::DeleteMessage;

				size_t idIndex = msg.find("target-msg-id");
				idIndex = msg.find('=', idIndex) + 1;

				deleteCmd.targetMsgId = msg.substr(idIndex, msg.find(';', idIndex) - idIndex);

				chatQueue.Push(deleteCmd);
			}
			else if (msg.find("CLEARCHAT") != std::string_view::npos)
			{
				ChatMessage clearCmd;

				size_t idIndex = msg.find("target-user-id");
				idIndex = msg.find('=', idIndex) + 1;

				if (idIndex != std::string_view::npos)
				{
					clearCmd.action = ChatQueueAction::DeleteUser;
					clearCmd.targetUserId = msg.substr(idIndex, msg.find(';', idIndex) - idIndex);
				}
				else
				{
					clearCmd.action = ChatQueueAction::ClearAll;
				}

				chatQueue.Push(clearCmd);
			}

			buffer.consume(buffer.size());
		}
	}
	catch (std::exception const& e)
	{
		std::cerr << "\n>>> Network Error: " << e.what() << std::endl;
	}
}

void TwitchBot::Render()
{
	bool newMessage = false;
	while (!chatQueue.Empty())
	{
		if (std::optional<ChatMessage> msgOpt = chatQueue.TryPop())
		{
			ChatMessage msg = std::move(msgOpt.value());

			if (msg.action == ChatQueueAction::AddMessage)
			{
				if (msg.text.starts_with('!')) { Commands::RunCommand(msg); }

				chatHistory.push_back(std::move(msg));
				newMessage = true;

				if (chatHistory.size() > MaxChatHistory)
				{
					chatHistory.pop_front();
				}
			}
			else if (msg.action == ChatQueueAction::DeleteMessage)
			{
				for (auto it = chatHistory.rbegin(); it != chatHistory.rend(); ++it)
				{
					if (it->id == msg.targetMsgId)
					{
						it->isDeleted = true;
						break;
					}
				}
			}
			else if (msg.action == ChatQueueAction::DeleteUser)
			{
				for (auto& m : chatHistory)
				{
					if (m.userId == msg.targetUserId)
					{
						m.isDeleted = true;
					}
				}
			}
			else if (msg.action == ChatQueueAction::ClearAll)
			{
				chatHistory.clear();
			}
		}
	}

	SongQueue::UpdateCurrentSong();

	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | 
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	ImGui::PushFont(boldFont);
	ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.788235294117647f, 0.62745098039f, 0.86274509803f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.788235294117647f, 0.62745098039f, 0.86274509803f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.788235294117647f, 0.62745098039f, 0.86274509803f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.75f));

	ImGui::Begin("Hababot2", nullptr, windowFlags);

	if (ImGui::BeginMenuBar())
	{
		ImGui::PushFont(font);
		if (ImGui::MenuItem("Settings"))
		{
			showSettings = !showSettings;
		}
		ImGui::PopFont();
		ImGui::EndMenuBar();
	}

	if (showOnboarding)
	{
		ImGui::OpenPopup("First Time Setup");
		showOnboarding = false;
	}

	ImGui::PopStyleColor(4);
	ImGui::PopFont();
	ImGui::PopStyleVar();

	RenderSettingsWindow();

	ImGui::PushFont(TwitchBot::font);
	Config::OnboardingPopup();
	Debug::RenderDebugTools();
	ImGui::PopFont();

	if (hypeTrain.isActive)
	{
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.56f, 0.27f, 0.97f, 1.0f));

		float fraction = 0.0f;
		if (hypeTrain.goal > 0)
		{
			fraction = (float)hypeTrain.progress / (float)hypeTrain.goal;
		}

		std::string overlayText = "Hype Train Level " + std::to_string(hypeTrain.level) + "!";

		ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlayText.c_str());

		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	constexpr float songQueueWidth = 300.0f;
	constexpr float columnGap = 8.0f;
	float availWidth = ImGui::GetContentRegionAvail().x;
	float chatWidth = std::max(150.0f, availWidth - songQueueWidth - columnGap);

	ImGui::BeginChild("ChatRegion", ImVec2(chatWidth, 0), false, ImGuiWindowFlags_NoScrollbar);

	float maxWrapX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	for (const ChatMessage& msg : chatHistory)
	{
		bool isOnlyJumbo = false;
		if (msg.isGigantified)
		{
			int contentCount = 0;
			for (const auto& seg : msg.segments)
			{
				std::visit([&](auto&& arg) {
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, EmoteSegment> || std::is_same_v<T, CheerSegment>) { ++contentCount; }
					else if constexpr (std::is_same_v<T, TextSegment>)
					{
						if (arg.text.find_first_not_of(" \t\r\n") != std::string::npos) { ++contentCount; }
					}
				}, seg);
			}
			isOnlyJumbo = (contentCount <= 1);
		}

		ImVec2 posMin = ImGui::GetCursorScreenPos();
		posMin.x -= 4.0f;
		posMin.y -= 2.0f;

		drawList->ChannelsSplit(2);
		drawList->ChannelsSetCurrent(1);

		if (!msg.headerText.empty())
		{
			ImGui::PushFont(headerFont);
			ImGui::PushStyleColor(ImGuiCol_Text, msg.headerColor);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
			ImGui::TextUnformatted(msg.headerText.c_str());
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}

		if (msg.isGigantified && isOnlyJumbo)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 2.5f);
		}

		if (!msg.isAlert) { DrawUsername(msg); }
		DrawMessage(msg);

		float adjustedY = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + 2.0f;
		ImVec2 posMax = ImVec2(maxWrapX, adjustedY);

		drawList->ChannelsSetCurrent(0);

		if (msg.hasBackground)
		{
			ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(msg.backgroundColor);
			drawList->AddRectFilled(posMin, posMax, bgColor);

			if (msg.hasAccentBar)
			{
				ImU32 accentColor = ImGui::ColorConvertFloat4ToU32(msg.accentColor);
				drawList->AddRectFilled(posMin, ImVec2(posMin.x + 3.0f, posMax.y), accentColor);
			}
		}

		drawList->ChannelsMerge();
	}

	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() || newMessage)
	{
		ImGui::SetScrollHereY(1.0f);
	}

	ImGui::EndChild();

	ImGui::SameLine(0.0f, columnGap);

	SongQueue::RenderSongQueue();

	ImGui::End();
	ImGui::Render();
}

void TwitchBot::RenderSettingsWindow()
{
	if (showSettings && !ImGui::IsPopupOpen("Settings"))
	{
		ImGui::OpenPopup("Settings");
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(660, 480), ImGuiCond_FirstUseEver);
	ImGui::PushFont(font);

	if (ImGui::BeginPopupModal("Settings", &showSettings, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::BeginTabBar("SettingsTabs"))
		{
			if (ImGui::BeginTabItem("Queue & General"))
			{
				ImGui::Spacing();
				ImGui::PushFont(boldFont);
				ImGui::TextUnformatted("Song Queue");
				ImGui::PopFont();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Checkbox("Enable Requests", &SongQueue::requestsEnabled))
				{
					SongQueue::BroadcastState();
				}

				if (ImGui::Checkbox("Show Queue", &SongQueue::queueVisible))
				{
					SongQueue::BroadcastState();
				}

				ImGui::Spacing();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Song Folders"))
			{
				ImGui::Spacing();
				Config::RenderPathEditor();

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Save & Rescan Songs", ImVec2(180.0f, 32.0f)))
				{
					auto& folders = Config::data.songFolders;
					folders.erase(std::remove_if(folders.begin(), folders.end(),
						[](const std::string& s) { return s.empty(); }), folders.end());

					Config::Save("config.json");
					SongLibrary::Initialize();
				}

				ImGui::SameLine();

				if (ImGui::Button("Revert Changes", ImVec2(130.0f, 32.0f)))
				{
					Config::Load("config.json");
				}

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Customize Commands"))
			{
				ImGui::Spacing();
				Commands::RenderCommandManager();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::EndPopup();
	}

	ImGui::PopFont();
}

void TwitchBot::DrawUsername(const ChatMessage& msg)
{
	float currentFontHeight = ImGui::GetFontSize();

	ImVec2 badgeSize(currentFontHeight, currentFontHeight);

	float baselineOffset = ImGui::GetCursorPosY() + (currentFontHeight * 0.15f);

	size_t start = 0;
	std::string badges = msg.rawBadgesTag;

	while (start < badges.length())
	{
		size_t comma = badges.find(',', start);
		std::string badgeKey = badges.substr(start, comma - start);

		if (ID3D11ShaderResourceView* srv = Badges::GetBadge(badgeKey))
		{
			ImGui::SetCursorPosY(baselineOffset);
			ImGui::Image((void*)srv, badgeSize);
			ImGui::SameLine();
		}

		if (comma == std::string::npos) { break; }
		start = comma + 1;
	}

	ImGui::SetCursorPosY(baselineOffset);
	ImVec4 userColor = HexToImvec4(msg.colorHex);
	ImGui::PushStyleColor(ImGuiCol_Text, userColor);
	ImGui::PushFont(boldFont);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
	ImGui::Text("%s", msg.displayName.c_str());
	ImGui::PopFont();
	ImGui::PopStyleColor();

	ImGui::SameLine();
	ImGui::SetCursorPosY(baselineOffset);

	ImGui::Text(msg.isAction ? " " : ": ");
	ImGui::PopStyleVar();
	ImGui::SameLine();
}

void TwitchBot::DrawMessage(const ChatMessage& msg)
{
	float maxWrapX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
	float currentFontHeight = ImGui::GetFontSize();
	ImVec2 emoteSize(currentFontHeight * 1.5f, currentFontHeight * 1.5f);
	float originalY = ImGui::GetCursorPosY();
	float spaceWidth = ImGui::CalcTextSize(" ").x;
	float offset = 0.0f;

	float totalEmoteWidth = emoteSize.x + spaceWidth;
	float spaceCount = emoteSize.x / spaceWidth;

	int lastEmoteIdx = -1;
	int contentCount = 0;
	if (msg.isGigantified)
	{
		for (size_t i = 0; i < msg.segments.size(); ++i)
		{
			std::visit([&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, EmoteSegment>)
				{
					lastEmoteIdx = static_cast<int>(i);
					++contentCount;
				}
				else if constexpr (std::is_same_v<T, CheerSegment>) { ++contentCount; }
				else if constexpr (std::is_same_v<T, TextSegment>)
				{
					if (arg.text.find_first_not_of(" \t\r\n") != std::string::npos) { ++contentCount; }
				}
			}, msg.segments[i]);
		}
	}
	bool isOnlyJumbo = (contentCount <= 1);

	ImGui::PushFont(msg.isAction ? italicFont : font);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));

	if (msg.isDeleted)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::TextWrapped("<message deleted>");
		ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	else
	{
		int currentSegIdx = 0;
		for (const ChatSegment& segment : msg.segments)
		{
			offset = 0.0f;
			std::visit([&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;

				if constexpr (std::is_same_v<T, TextSegment>)
				{
					std::string text = arg.text;
					size_t start = 0;

					while (start < text.length())
					{
						size_t nextSpace = text.find(' ', start);
						size_t wordEnd = (nextSpace == std::string::npos) ? text.length() : nextSpace + 1;

						std::string word = text.substr(start, wordEnd - start);
						float wordWidth = ImGui::CalcTextSize(word.c_str()).x;

						if (ImGui::GetCursorScreenPos().x + wordWidth > maxWrapX && ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x)
						{
							ImGui::NewLine();
							originalY = ImGui::GetCursorPosY();
						}

						ImGui::SetCursorPosY(originalY);
						ImGui::Text("%s", word.c_str());
						ImGui::SameLine();

						start = wordEnd;
					}
				}
				else if constexpr (std::is_same_v<T, EmoteSegment>)
				{
					bool isJumbo = (msg.isGigantified && currentSegIdx == lastEmoteIdx);

					if (isJumbo && !isOnlyJumbo)
					{
						ImGui::NewLine();
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + currentFontHeight * 2.5f);
						originalY = ImGui::GetCursorPosY();
					}

					ImVec2 currentEmoteSize = isJumbo ? ImVec2(emoteSize.x * 4.0f, emoteSize.y * 4.0f) : emoteSize;
					float currentTotalEmoteWidth = currentEmoteSize.x + spaceWidth;

					if (ImGui::GetCursorScreenPos().x + currentTotalEmoteWidth > maxWrapX &&
						ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x)
					{
						ImGui::NewLine();
						originalY = ImGui::GetCursorPosY();
					}

					if (ID3D11ShaderResourceView* srv = Emotes::GetEmote(arg.emoteId, isJumbo))
					{
						ID3D11Resource* res = nullptr;
						srv->GetResource(&res);
						ID3D11Texture2D* tex = nullptr;
						res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
						D3D11_TEXTURE2D_DESC desc;
						tex->GetDesc(&desc);
						tex->Release();
						res->Release();

						float yOffset = isJumbo ? (currentFontHeight * 2.5f) : (currentFontHeight * 0.15f);
						ImGui::SetCursorPosY(originalY - yOffset);
						ImGui::Image((void*)srv, currentEmoteSize);

						if (isJumbo)
						{
							originalY = originalY + currentFontHeight * 3.5f;
							ImGui::SetCursorPosY(originalY);
						}
						else
						{
							ImGui::SetCursorPosY(originalY);
						}
					}
					else
					{
						ImGui::SetCursorPosY(originalY);
						offset = ImGui::GetCursorPosX() + currentEmoteSize.x;
					}

					ImGui::SameLine(offset);
				}
				else if constexpr (std::is_same_v<T, CheerSegment>)
				{
					float spaceWidth = ImGui::CalcTextSize(" ").x;

					ImGui::PushFont(boldFont);
					float amountWidth = ImGui::CalcTextSize(arg.amountText.c_str()).x;
					ImGui::PopFont();

					float totalCheerWidth = emoteSize.x + amountWidth + spaceWidth;

					if (ImGui::GetCursorScreenPos().x + totalCheerWidth > maxWrapX &&
						ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x)
					{
						ImGui::NewLine();
						originalY = ImGui::GetCursorPosY();
					}

					if (ID3D11ShaderResourceView* srv = Emotes::GetEmote(arg.url))
					{
						ImGui::SetCursorPosY(originalY - (currentFontHeight * 0.15f));
						ImGui::Image((void*)srv, emoteSize);
						ImGui::SetCursorPosY(originalY);
					}
					else
					{
						ImGui::SetCursorPosY(originalY);
						offset = ImGui::GetCursorPosX() + emoteSize.x;
					}

					ImGui::SameLine(offset);

					ImGui::PushFont(boldFont);
					ImGui::PushStyleColor(ImGuiCol_Text, arg.color);
					ImGui::Text("%s", arg.amountText.c_str());
					ImGui::PopStyleColor();
					ImGui::PopFont();

					ImGui::SameLine();
				}
			}, segment);

			++currentSegIdx;
		}
	}

	ImGui::PopFont();
	ImGui::PopStyleVar();
	ImGui::NewLine();
}

void TwitchBot::SendMessage(const std::string& text)
{
	ChatMessage localEcho;
	localEcho.username = bot.displayName;
	localEcho.displayName = bot.displayName;
	localEcho.colorHex = bot.color;
	localEcho.text = text;
	localEcho.mod = bot.mod;
	localEcho.broadcaster = bot.broadcaster;
	localEcho.vip = bot.vip;
	localEcho.subscriber = bot.subscriber;
	localEcho.rawBadgesTag = bot.rawBadges;
	localEcho.segments.push_back(TextSegment{ localEcho.text });

	if (IsExactMention(localEcho.text))
	{
		localEcho.hasBackground = true;
		localEcho.backgroundColor = ImVec4(0.8f, 0.2f, 0.2f, 0.5f);
	}

	chatQueue.Push(std::move(localEcho));

	std::string rawIrc = "PRIVMSG #" + OAuth::ChannelName() + " :" + text + "\r\n";

	asio::post(*ioc, [msg = std::move(rawIrc)]() mutable {
		bool writeInProgress = !writeQueue.empty();

		writeQueue.push(std::move(msg));

		if (!writeInProgress)
		{
			asio::co_spawn(*ioc, WriteLoop(), asio::detached);
		}
	});
}

void TwitchBot::SendAlert(ChatMessage& alert)
{
	chatQueue.Push(std::move(alert));
}

void TwitchBot::NotifyRaid(const std::string& fromChannelName)
{
	std::lock_guard<std::mutex> lock(mtx);
	activeRaid.raiderChannel = fromChannelName;
	activeRaid.expiresAt = std::chrono::steady_clock::now() + std::chrono::minutes(5);
	confirmedRaiders.clear();

	std::string lowerRaider = fromChannelName;
	std::transform(lowerRaider.begin(), lowerRaider.end(), lowerRaider.begin(),
		[](unsigned char c) { return std::tolower(c); });
	knownChatters.erase(lowerRaider);
}

void TwitchBot::BanUser(const std::string& targetUserId, int durationSeconds, const std::string& reason)
{
	if (targetUserId.empty()) { return; }

	std::thread([targetUserId, durationSeconds, reason]() {
		std::string broadcasterId = OAuth::BroadcasterId();
		std::string url = "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=" + broadcasterId + "&moderator_id=" + broadcasterId;

		nlohmann::json data = {
			{ "user_id", targetUserId },
			{ "reason", reason }
		};

		if (durationSeconds > 0)
		{
			data["duration"] = durationSeconds;
		}

		nlohmann::json payload = { { "data", data } };

		cpr::Post(
			cpr::Url{ url },
			cpr::Header{
				{ "Authorization", "Bearer " + OAuth::AccessToken() },
				{ "Client-Id", OAuth::ClientId() },
				{ "Content-Type", "application/json" }
			},
			cpr::Body{ payload.dump() }
		);
	}).detach();
}

void TwitchBot::DeleteChatMessage(const std::string& messageId)
{
	if (messageId.empty()) { return; }

	std::thread([messageId]() {
		std::string broadcasterId = OAuth::BroadcasterId();
		std::string url = "https://api.twitch.tv/helix/moderation/chat?broadcaster_id=" + broadcasterId +
			"&moderator_id=" + broadcasterId + "&message_id=" + messageId;

		cpr::Delete(
			cpr::Url{ url },
			cpr::Header{
				{ "Authorization", "Bearer " + OAuth::AccessToken() },
				{ "Client-Id", OAuth::ClientId() }
			}
		);
	}).detach();
}

void TwitchBot::LoadEarlyBirdStats()
{
	std::ifstream file("early_birds.json");

	if (file.is_open())
	{
		try
		{
			nlohmann::json j;
			file >> j;

			for (auto& [user, stats] : j.items())
			{
				earlyBirdLeaderboard[user] = {
					stats.value("first", 0),
					stats.value("second", 0),
					stats.value("third", 0)
				};
			}
		}
		catch (...) { std::cerr << "Failed to parse early_birds.json\n"; }
	}
}

void TwitchBot::SaveEarlyBirdStats()
{
	nlohmann::json j;
	for (const auto& [user, stats] : earlyBirdLeaderboard)
	{
		j[user] = {
			{ "first", stats.firstPlace },
			{ "second", stats.secondPlace },
			{ "third", stats.thirdPlace },
		};
	}

	std::ofstream file("early_birds.json");
	if (file.is_open())
	{
		file << j.dump(4);
	}
}

std::optional<ChatMessage> TwitchBot::ParseMsg(std::string_view raw)
{
	ChatMessage msg{};

	if (raw.starts_with('@'))
	{
		size_t tagsEnd = raw.find(" :");
		if (tagsEnd == std::string_view::npos) { return std::nullopt; }

		std::string_view tagsStr = raw.substr(1, tagsEnd - 1);

		raw.remove_prefix(tagsEnd + 1);

		size_t pos = 0;
		while (pos < tagsStr.length())
		{
			size_t semi = tagsStr.find(';', pos);
			std::string_view kv = tagsStr.substr(pos, semi - pos);

			size_t eq = kv.find('=');
			if (eq != std::string_view::npos)
			{
				std::string_view key = kv.substr(0, eq);
				std::string_view val = kv.substr(eq + 1);

				//TODO: Hash strings and use switch-case
				if (key == "id") { msg.id = val; }
				else if (key == "user-id") { msg.userId = val; }
				else if (key == "display-name") { msg.displayName = val; }
				else if (key == "color") { msg.colorHex = val; }

				else if (key == "tmi-sent-ts") { std::from_chars(val.data(), val.data() + val.size(), msg.timestamp); }
				else if (key == "bits") { std::from_chars(val.data(), val.data() + val.size(), msg.bits); }

				else if (key == "mod") { msg.mod = (val == "1"); }
				else if (key == "vip") { msg.vip = (val == "1"); }
				else if (key == "turbo") { msg.turbo = (val == "1"); }
				else if (key == "first-msg") { msg.firstMsg = (val == "1"); }
				else if (key == "returning-chatter") { msg.returning = (val == "1"); }
				else if (key == "subscriber") { msg.subscriber = (val == "1"); }
				else if (key == "badges")
				{
					msg.rawBadgesTag = val;

					if (val.find("broadcaster") != std::string_view::npos)
					{
						msg.broadcaster = true;
					}
				}

				else if (key == "badge-info") { msg.badgeInfo = val; }
				else if (key == "custom-reward-id") { msg.customRewardId = val; }
				else if (key == "emotes") { msg.emotes = ParseEmotes(std::string{ val }); }

				else if (key == "reply-parent-msg-id") { msg.replyParentId = val; }
				else if (key == "reply-parent-display-name") { msg.replyParentUser = val; }
				else if (key == "reply-parent-msg-body") { msg.replyParentMsg = val; }

				else if (key == "msg-id")
				{
					if (val == "highlighted-message")
					{
						msg.headerText = "Highlighted Message";
						msg.headerColor = ImVec4(0.4f, 0.9f, 1.0f, 1.0f);
						msg.hasBackground = true;
						msg.backgroundColor = ImVec4(0.15f, 0.50f, 0.65f, 0.35f);
						msg.hasAccentBar = true;
						msg.accentColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f);
					}
					else if (val == "gigantified-emote-message")
					{
						msg.isGigantified = true;
					}
				}
				else if (key == "animation-id") { msg.messageEffect = val; }
			}
			if (semi == std::string_view::npos) { break; }
			pos = semi + 1;
		}
	}

	if (raw.starts_with(':'))
	{
		size_t bang = raw.find('!');
		if (bang != std::string_view::npos)
		{
			msg.username = raw.substr(1, bang - 1);
		}

		size_t textStart = raw.find(" PRIVMSG ");
		if (textStart != std::string_view::npos)
		{
			textStart = raw.find(" :", textStart);
			if (textStart != std::string_view::npos)
			{
				msg.text = raw.substr(textStart + 2);

				while (!msg.text.empty() && (msg.text.back() == '\r' || msg.text.back() == '\n'))
				{
					msg.text.pop_back();
				}

				constexpr std::string_view actionPrefix = "\x01""ACTION ";
				if (msg.text.starts_with(actionPrefix) && msg.text.ends_with('\x01'))
				{
					msg.isAction = true;
					msg.text.erase(0, actionPrefix.length());
					msg.text.pop_back();
				}
			}
		}
	}

	if (msg.emotes.size())
	{
		msg.segments.clear();

		int currentStringIndex = 0;

		for (const auto& placement : msg.emotes)
		{
			if (placement.startIndex > currentStringIndex)
			{
				AppendTextWithEmojis(msg.segments, msg.text.substr(currentStringIndex, placement.startIndex - currentStringIndex), msg.bits > 0);
			}

			msg.segments.push_back(EmoteSegment{ placement.imageId });

			currentStringIndex = placement.endIndex + 1;
		}

		if (currentStringIndex < msg.text.length())
		{
			AppendTextWithEmojis(msg.segments, msg.text.substr(currentStringIndex), msg.bits > 0);
		}
	}
	else
	{
		AppendTextWithEmojis(msg.segments, msg.text, msg.bits > 0);
	}

	if (msg.displayName.empty()) { msg.displayName = msg.username; }
	if (msg.colorHex.empty()) { msg.colorHex = "#FFFFFF"; }
	if (msg.username.empty() || msg.text.empty()) { return std::nullopt; }

	bool isRaider = false;
	std::string raidSource;
	{
		std::lock_guard<std::mutex> lock(mtx);
		if (std::chrono::steady_clock::now() < activeRaid.expiresAt)
		{
			if (!knownChatters.contains(msg.username))
			{
				confirmedRaiders.insert(msg.username);
			}
			if (confirmedRaiders.contains(msg.username))
			{
				isRaider = true;
				raidSource = activeRaid.raiderChannel;
			}
		}
		knownChatters.insert(msg.username);
	}

	if (msg.firstMsg)
	{
		msg.headerText = isRaider ? ("First Time Chatter From " + raidSource) : "First Time Chatter";
		msg.headerColor = ImVec4(0.85f, 0.70f, 1.0f, 1.0f);
		msg.hasBackground = true;
		msg.backgroundColor = ImVec4(0.93f, 0.59f, 0.84f, 0.50f);
		msg.hasAccentBar = true;
		msg.accentColor = ImVec4(0.70f, 0.40f, 1.0f, 1.0f);
	}
	else if (isRaider)
	{
		msg.headerText = "Raider From " + raidSource;
		msg.headerColor = ImVec4(1.0f, 0.80f, 0.40f, 1.0f);
		msg.hasBackground = true;
		msg.backgroundColor = ImVec4(0.85f, 0.50f, 0.10f, 0.30f);
		msg.hasAccentBar = true;
		msg.accentColor = ImVec4(1.0f, 0.65f, 0.15f, 1.0f);
	}
	else if (msg.returning)
	{
		msg.headerText = "Returning Chatter";
		msg.headerColor = ImVec4(0.50f, 0.85f, 1.0f, 1.0f);
		msg.hasBackground = true;
		msg.backgroundColor = ImVec4(0.15f, 0.45f, 0.85f, 0.30f);
		msg.hasAccentBar = true;
		msg.accentColor = ImVec4(0.30f, 0.70f, 1.0f, 1.0f);
	}
	else if (!msg.replyParentUser.empty())
	{
		std::string preview = msg.replyParentMsg;

		size_t pos = 0;

		while ((pos = preview.find('\\', pos)) != std::string::npos)
		{
			preview.replace(pos, 2, " ");
			pos += 2;
		}

		if (preview.length() > 40) { preview = preview.substr(0, 37) + "..."; }
		msg.headerText = "Replying to @" + msg.replyParentUser + ": " + preview;
		msg.headerColor = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
		size_t i = msg.text.find_first_of(' ') + 1;
		msg.text = msg.text.substr(i);
		ChatSegment& seg = msg.segments.front();

		std::visit([&](auto&& arg) {
			if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, TextSegment>)
			{
				arg.text = arg.text.substr(i);
			}
		}, seg);

		if (msg.replyParentUser == OAuth::ChannelName())
		{
			msg.hasBackground = true;
			msg.backgroundColor = ImVec4(0.8f, 0.2f, 0.2f, 0.5f);
		}
	}

	if (IsExactMention(msg.text))
	{
		msg.hasBackground = true;
		msg.backgroundColor = ImVec4(0.8f, 0.2f, 0.2f, 0.5f);
	}

	AutoModAction verdict = EvaluateMessage(msg);

	if (verdict == AutoModAction::Ban)
	{
		BanUser(msg.userId, 0, msg.autoModReason);
		return std::nullopt;
	}
	else if (verdict == AutoModAction::Timeout)
	{
		BanUser(msg.userId, 60, msg.autoModReason);
		return std::nullopt;
	}
	else if (verdict == AutoModAction::Hold)
	{
		DeleteChatMessage(msg.id);
	}

	return msg;
}

bool TwitchBot::IsEmojiCodepoint(uint32_t cp)
{
	return (cp >= 0x1F000 && cp <= 0x1FAFF) ||
		(cp >= 0x2600 && cp <= 0x27BF) ||
		(cp >= 0x2300 && cp <= 0x23FF) ||
		cp == 0x203C || cp == 0x2049 ||
		(cp >= 0x2194 && cp <= 0x21AA) ||
		(cp >= 0x25AA && cp <= 0x25FE) ||
		(cp >= 0x2B05 && cp <= 0x2B55) ||
		cp == 0x3030 || cp == 0x303D ||
		cp == 0x3297 || cp == 0x3299;
}

std::optional<CheerSegment> TwitchBot::TryParseCheermote(std::string_view word)
{
	if (word.size() < 2) { return std::nullopt; }

	size_t digitStart = word.size();
	while (digitStart > 0 && std::isdigit(static_cast<unsigned char>(word[digitStart - 1])))
	{
		--digitStart;
	}

	if (digitStart == 0 || digitStart == word.size())
	{
		return std::nullopt;
	}

	std::string prefix(word.substr(0, digitStart));
	std::transform(prefix.begin(), prefix.end(), prefix.begin(),
		[](unsigned char c) { return std::tolower(c); });

	auto it = cheermotes.find(prefix);
	if (it == cheermotes.end()) { return std::nullopt; }

	std::string_view amountView = word.substr(digitStart);
	int amount = 0;
	auto [ptr, ec] = std::from_chars(amountView.data(), amountView.data() + amountView.size(), amount);
	if (ec != std::errc{} || amount <= 0) { return std::nullopt; }

	for (const auto& tier : it->second)
	{
		if (amount >= tier.minBits)
		{
			return CheerSegment{ tier.url, std::string(amountView), tier.color };
		}
	}

	return std::nullopt;
}

void TwitchBot::AppendTextWithEmojis(std::vector<ChatSegment>& segments, const std::string& text, bool hasBits)
{
	std::string currentText;
	size_t i = 0;

	while (i < text.length())
	{
		if (hasBits && (i == 0 || text[i - 1] == ' '))
		{
			size_t wordEnd = text.find(' ', i);
			if (wordEnd == std::string::npos) { wordEnd = text.length(); }

			std::string_view word(text.data() + i, wordEnd - i);
			if (std::optional<CheerSegment> cheerSeg = TryParseCheermote(word))
			{
				if (!currentText.empty())
				{
					segments.push_back(TextSegment{ currentText });
					currentText.clear();
				}

				segments.push_back(std::move(cheerSeg.value()));
				i = wordEnd;
				continue;
			}
		}

		unsigned char c = static_cast<unsigned char>(text[i]);
		uint32_t cp = 0;
		size_t charLen = 1;

		if ((c & 0x80) == 0) { cp = c; charLen = 1; }
		else if ((c & 0xE0) == 0xC0 && i + 1 < text.length())
		{
			cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
			charLen = 2;
		}
		else if ((c & 0xF0) == 0xE0 && i + 2 < text.length())
		{
			cp = ((c & 0x0F) << 12) |
				((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
				(static_cast<unsigned char>(text[i + 2]) & 0x3F);
			charLen = 3;
		}
		else if ((c & 0xF8) == 0xF0 && i + 3 < text.length())
		{
			cp = ((c & 0x07) << 18) |
				((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
				((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
				(static_cast<unsigned char>(text[i + 3]) & 0x3F);
			charLen = 4;
		}

		if (cp == 0xFE0F)
		{
			i += charLen;
			continue;
		}

		if (IsEmojiCodepoint(cp))
		{
			if (!currentText.empty())
			{
				segments.push_back(TextSegment{ currentText });
				currentText.clear();
			}

			std::ostringstream oss;
			oss << "emoji_" << std::hex << std::nouppercase << cp;
			segments.push_back(EmoteSegment{ oss.str() });
		}
		else
		{
			currentText.append(text, i, charLen);
		}

		i += charLen;
	}

	if (!currentText.empty())
	{
		segments.push_back(TextSegment{ currentText });
	}
}

bool TwitchBot::IsExactMention(const std::string& message)
{
	std::string lowerMsg = message;
	std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(),
		[](unsigned char c) { return std::tolower(c); });

	std::string lowerTarget = OAuth::ChannelName();
	std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(),
		[](unsigned char c) { return std::tolower(c); });

	size_t pos = 0;
	while ((pos = lowerMsg.find(lowerTarget, pos)) != std::string::npos)
	{
		bool leftOk = (pos == 0) || !std::isalnum(static_cast<unsigned char>(lowerMsg[pos - 1]));

		size_t endPos = pos + lowerTarget.length();
		bool rightOk = (endPos == lowerMsg.length()) || !std::isalnum(static_cast<unsigned char>(lowerMsg[endPos]));

		if (leftOk && rightOk) { return true; }

		pos = endPos;
	}

	return false;
}

std::vector<EmotePlacement> TwitchBot::ParseEmotes(const std::string& rawEmotes)
{
	std::vector<EmotePlacement> placements;
	if (rawEmotes.empty()) { return placements; }

	size_t pos = 0;
	while (pos < rawEmotes.length())
	{
		size_t colonPos = rawEmotes.find(':', pos);
		if (colonPos == std::string::npos) { break; }

		std::string emoteId = rawEmotes.substr(pos, colonPos - pos);

		pos = colonPos + 1;

		while (pos < rawEmotes.length())
		{
			size_t dashPos = rawEmotes.find('-', pos);
			size_t commaPos = rawEmotes.find(',', pos);
			size_t slashPos = rawEmotes.find('/', pos);

			size_t endOfPair = std::min(commaPos, slashPos);

			if (endOfPair == std::string::npos)
			{
				endOfPair = rawEmotes.length();
			}

			if (dashPos != std::string::npos && dashPos < endOfPair)
			{
				int startIdx = std::stoi(rawEmotes.substr(pos, dashPos - pos));
				int endIdx = std::stoi(rawEmotes.substr(dashPos + 1, endOfPair - (dashPos + 1)));

				placements.push_back({ emoteId, startIdx, endIdx });
			}

			if (endOfPair == commaPos) { pos = commaPos + 1; }
			else if (endOfPair == slashPos) { pos = slashPos + 1; break; }
			else { break; }
		}
	}

	std::sort(placements.begin(), placements.end(), [](const EmotePlacement& a, const EmotePlacement& b) {
		return a.startIndex < b.startIndex;
	});

	return placements;
}

void TwitchBot::ParseState(std::string_view raw, TwitchUser& user)
{
	if (raw.starts_with('@'))
	{
		size_t tagsEnd = raw.find(" :");
		if (tagsEnd == std::string_view::npos) { return; }

		std::string_view tagsStr = raw.substr(1, tagsEnd - 1);

		raw.remove_prefix(tagsEnd + 1);

		size_t pos = 0;
		while (pos < tagsStr.length())
		{
			size_t semi = tagsStr.find(';', pos);
			std::string_view kv = tagsStr.substr(pos, semi - pos);

			size_t eq = kv.find('=');
			if (eq != std::string_view::npos)
			{
				std::string_view key = kv.substr(0, eq);
				std::string_view val = kv.substr(eq + 1);

				//TODO: Hash strings and use switch-case
				if (key == "id") { user.id = val; }
				else if (key == "display-name") { user.displayName = val; }
				else if (key == "color") { user.color = val; }

				else if (key == "mod") { user.mod = (val == "1"); }
				else if (key == "vip") { user.vip = (val == "1"); }
				else if (key == "turbo") { user.turbo = (val == "1"); }
				else if (key == "first-msg") { user.firstMsg = (val == "1"); }
				else if (key == "returning-chatter") { user.returning = (val == "1"); }
				else if (key == "subscriber") { user.subscriber = (val == "1"); }
				else if (key == "badges")
				{
					user.rawBadges = val;

					if (val.find("broadcaster") != std::string_view::npos)
					{
						user.broadcaster = true;
					}
				}

				else if (key == "badge-info") { user.badgeInfo = val; }
			}
			if (semi == std::string_view::npos) { break; }
			pos = semi + 1;
		}
	}

	if (user.color.empty()) { user.color = "#FFFFFF"; }
}

std::string TwitchBot::NormalizeForSpamCheck(std::string_view input)
{
	std::string out;
	out.reserve(input.size());

	for (size_t i = 0; i < input.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(input[i]);

		if (c == 0xD0 && i + 1 < input.size())
		{
			unsigned char next = static_cast<unsigned char>(input[++i]);
			if (next == 0xB0 || next == 0x90) { out.push_back('a'); }
			else if (next == 0xB5 || next == 0x95) { out.push_back('e'); }
			else if (next == 0xBE || next == 0x9E) { out.push_back('o'); }
			else if (next == 0xBA || next == 0x9A) { out.push_back('k'); }
			continue;
		}
		if (c == 0xD1 && i + 1 < input.size())
		{
			unsigned char next = static_cast<unsigned char>(input[++i]);
			if (next == 0x81 || next == 0xA1) { out.push_back('c'); }
			else if (next == 0x80 || next == 0xA0) { out.push_back('p'); }
			else if (next == 0x85 || next == 0xA5) { out.push_back('x'); }
			else if (next == 0x83 || next == 0xA3) { out.push_back('y'); }
			continue;
		}

		if (std::isalnum(c))
		{
			out.push_back(static_cast<char>(std::tolower(c)));
		}
	}
	return out;
}

AutoModAction TwitchBot::EvaluateMessage(ChatMessage& msg)
{
	if (msg.broadcaster || msg.mod || msg.vip || msg.subscriber)
	{
		return AutoModAction::Allow;
	}

	std::string compacted = NormalizeForSpamCheck(msg.text);

	static constexpr std::array<std::string_view, 9> botBanPhrases = {
		"streamboo", "nezhna", "bestviewers", "cheapviewers",
		"buyfollowers", "buyviewers", "twitchprimes", "getviewers",
		"viewerbot"
	};

	for (std::string_view phrase : botBanPhrases)
	{
		if (compacted.find(phrase) != std::string::npos)
		{
			msg.autoModReason = "Bot Phrase (" + std::string(phrase) + ")";
			return AutoModAction::Ban;
		}
	}

	int brailleOrCombiningCount = 0;
	for (size_t i = 0; i + 2 < msg.text.size(); ++i)
	{
		unsigned char b0 = static_cast<unsigned char>(msg.text[i]);
		unsigned char b1 = static_cast<unsigned char>(msg.text[i + 1]);
		if (b0 == 0xE2 && (b1 >= 0xA0 && b1 <= 0xA3))
		{
			++brailleOrCombiningCount;
		}
	}

	if (brailleOrCombiningCount > 15)
	{
		msg.autoModReason = "ASCII / Braille Flood";
		return msg.firstMsg ? AutoModAction::Ban : AutoModAction::Timeout;
	}

	if (msg.firstMsg)
	{
		static constexpr std::array<std::string_view, 6> linkIndicators = {
			"http://", "https://", "www.", ".com", ".gg/", ".xyz"
		};

		std::string lowerRaw = msg.text;
		std::transform(lowerRaw.begin(), lowerRaw.end(), lowerRaw.begin(),
			[](unsigned char c) { return std::tolower(c); });

		for (std::string_view link : linkIndicators)
		{
			if (lowerRaw.find(link) != std::string::npos)
			{
				msg.autoModReason = "First-Chat Link";
				return AutoModAction::Hold;
			}
		}

		//static constexpr std::array<std::string_view, 5> solicitationPhrases = {
		//	"graphicdesigner", "digitalartist", "checkmydm", "makeoverlays", "customemotes"
		//};
		//
		//for (std::string_view sol : solicitationPhrases)
		//{
		//	if (compacted.find(sol) != std::string::npos)
		//	{
		//		msg.autoModReason = "Suspected Solicitation";
		//		return AutoModAction::Hold;
		//	}
		//}
	}

	return AutoModAction::Allow;
}

awaitable<void> TwitchBot::WriteLoop()
{
	while (!writeQueue.empty())
	{
		co_await ws->async_write(asio::buffer(writeQueue.front()), use_awaitable);
		writeQueue.pop();
	}
}

ImVec4 TwitchBot::HexToImvec4(const std::string& hex)
{
	if (hex.empty() || hex[0] != '#' || hex.length() != 7)
	{
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	try
	{
		int rgb = std::stoi(hex.substr(1), nullptr, 16);

		float r = ((rgb >> 16) & 0xFF) / 255.0f;
		float g = ((rgb >> 8) & 0xFF) / 255.0f;
		float b = (rgb & 0xFF) / 255.0f;

		float h, s, v;
		ImGui::ColorConvertRGBtoHSV(r, g, b, h, s, v);
		if (v < 0.5f)
		{
			v = 0.5f;
			ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
		}

		return ImVec4(r, g, b, 1.0f);
	}
	catch (...)
	{
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
}