#pragma once

#include "Queue.hpp"

#include "imgui.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

#include <queue>
#include <string>
#include <chrono>
#include <filesystem>
#include <unordered_set>

#undef SendMessage

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

using asio::awaitable;
using asio::use_awaitable;

enum class ChatQueueAction
{
	AddMessage,
	DeleteMessage,
	DeleteUser,
	ClearAll
};

enum class AutoModAction
{
	Allow,
	Hold,
	Timeout,
	Ban
};

struct TwitchUser
{
	std::string id;
	std::string login;
	std::string displayName;
	std::string color = "#FFFFFF";

	std::string badgeInfo;
	std::string rawBadges = "";

	bool mod = false;
	bool vip = false;
	bool turbo = false;
	bool firstMsg = false;
	bool returning = false;
	bool subscriber = false;
	bool broadcaster = false;
};

struct EmotePlacement
{
	std::string imageId;
	int startIndex = 0;
	int endIndex = 0;
};

struct CheermoteTier
{
	int minBits = 0;
	std::string url;
	ImVec4 color;
};

struct TextSegment
{
	std::string text;
};

struct EmoteSegment
{
	std::string emoteId;
};

struct CheerSegment
{
	std::string url;
	std::string amountText;
	ImVec4 color;
};

using ChatSegment = std::variant<TextSegment, EmoteSegment, CheerSegment>;

struct ChatMessage
{
	ChatQueueAction action = ChatQueueAction::AddMessage;

	std::string targetMsgId;
	std::string targetUserId;
	std::string autoModReason;
	bool isDeleted = false;
	bool isAction = false;
	bool isAlert = false;
	bool isGigantified = false;

	std::string id;
	std::string userId;
	std::string username;
	std::string displayName;
	std::string colorHex;

	std::string text;
	std::vector<ChatSegment> segments;
	uint64_t timestamp = 0;
	uint32_t bits = 0;

	bool hasBackground = false;
	ImVec4 backgroundColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	std::string headerText;
	ImVec4 headerColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	bool hasAccentBar = false;
	ImVec4 accentColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

	bool mod = false;
	bool vip = false;
	bool turbo = false;
	bool firstMsg = false;
	bool returning = false;
	bool subscriber = false;
	bool broadcaster = false;

	std::string badgeInfo;
	std::string rawBadgesTag;
	std::string customRewardId;
	std::string messageEffect;
	std::vector<EmotePlacement> emotes;

	std::string replyParentId;
	std::string replyParentUser;
	std::string replyParentMsg;
};

struct RaidTrackerState
{
	std::string raiderChannel;
	std::chrono::steady_clock::time_point expiresAt;
};

struct HypeTrainState
{
	bool isActive = false;
	int level = 0;
	int progress = 0;
	int goal = 100;
};

struct EarlyBirdStats
{
	int firstPlace = 0;
	int secondPlace = 0;
	int thirdPlace = 0;
};

struct TwitchTokens;

class TwitchBot
{
public:
	static void Initialize(asio::io_context& ioc, asio::ssl::context& ctx);
	static awaitable<void> Run();
	static void Render();

	static void SendMessage(const std::string& text);
	static void SendAlert(ChatMessage& alert);
	static void NotifyRaid(const std::string& fromChannelName);

	static void BanUser(const std::string& targetUserId, int durationSeconds = 0, const std::string& reason = "Spam bot");
	static void DeleteChatMessage(const std::string& messageId);

	static void LoadEarlyBirdStats();
	static void SaveEarlyBirdStats();

private:
	static constexpr size_t MaxChatHistory = 200;

	static asio::awaitable<void> ConnectToRelay();
	static asio::awaitable<void> RelayWriteLoop();
	static void SendToRelay(const std::string& msg);

	static void RenderSettingsWindow();
	static void DrawUsername(const ChatMessage& msg);
	static void DrawMessage(const ChatMessage& msg);

	static void FetchCheermotes();
	static ImVec4 HexToImvec4(const std::string& hex);

	static std::optional<ChatMessage> ParseMsg(std::string_view raw);
	static bool IsEmojiCodepoint(uint32_t cp);
	static std::optional<CheerSegment> TryParseCheermote(std::string_view word);
	static void AppendTextWithEmojis(std::vector<ChatSegment>& segments, const std::string& text, bool hasBits = false);
	static bool IsExactMention(const std::string& message);
	static std::vector<EmotePlacement> ParseEmotes(const std::string& rawEmotes);
	static void ParseState(std::string_view raw, TwitchUser& user);
	static AutoModAction EvaluateMessage(ChatMessage& msg);
	static std::string NormalizeForSpamCheck(std::string_view input);
	static awaitable<void> WriteLoop();

	static bool isRunning;
	static bool showSettings;
	static bool showOnboarding;
	static bool showCommandManager;
	static bool relayWriteInProgress;
	static std::atomic<bool> relayConnected;
	static std::mutex mtx;
	static TwitchUser bot;

	static ImFont* font;
	static ImFont* boldFont;
	static ImFont* italicFont;
	static ImFont* headerFont;

	static asio::io_context* ioc;
	static websocket::stream<beast::ssl_stream<asio::ip::tcp::socket>>* ws;
	static websocket::stream<beast::ssl_stream<asio::ip::tcp::socket>>* relayWs;
	static std::queue<std::string> relayWriteQueue;

	static std::unordered_map<std::string, std::vector<CheermoteTier>> cheermotes;
	static std::unordered_set<std::string> knownChatters;
	static std::unordered_set<std::string> confirmedRaiders;
	static ThreadSafeQueue<ChatMessage> chatQueue;
	static std::queue<std::string> writeQueue;
	static std::deque<ChatMessage> chatHistory;
	static RaidTrackerState activeRaid;
	static HypeTrainState hypeTrain;

	static int earlyBirdCount;
	static std::unordered_map<std::string, EarlyBirdStats> earlyBirdLeaderboard;

	friend class Events;
	friend class SongQueue;
	friend class SongLibrary;
	friend class Commands;
	friend class Debug;
};