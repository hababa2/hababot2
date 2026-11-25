#include "Events.hpp"

#include "OAuth.hpp"
#include "TwitchBot.hpp"
#include "Config.hpp"
#include "Theme.hpp"

#include <cpr/cpr.h>

#include <iostream>

namespace
{
	struct PendingGift
	{
		std::string gifter;
		std::string userId;
		std::string username;
		std::string colorHex = "#9146FF";
		std::string tier = "Tier 1";
		int expectedTotal = 0;
		int cumulativeTotal = 0;
		bool hasSummary = false;
		std::vector<std::string> recipients;
	};

	std::optional<asio::any_io_executor> eventExecutor;
	std::mutex pendingGiftMtx;
	std::unordered_map<std::string, PendingGift> pendingGifts;

	std::string JoinRecipients(const std::vector<std::string>& names)
	{
		if (names.empty()) { return ""; }
		if (names.size() == 1) { return names[0]; }
		if (names.size() == 2) { return names[0] + " and " + names[1]; }

		std::string out;
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (i > 0)
			{
				out += (i + 1 == names.size()) ? ", and " : ", ";
			}
			out += names[i];
		}
		return out;
	}

	void SendGiftCard(const PendingGift& gift)
	{
		ChatMessage msg{};
		msg.isAlert = true;
		msg.userId = gift.userId;
		msg.username = gift.username;
		msg.displayName = gift.gifter;
		msg.colorHex = gift.colorHex;

		msg.cardType = ChatCardType::Gift;

		int count = std::max(gift.expectedTotal, static_cast<int>(gift.recipients.size()));

		if (count <= 1 && gift.recipients.size() == 1)
		{
			msg.headerText = "Gift Sub • " + gift.tier;
			msg.text = gift.gifter + " gifted a " + gift.tier + " sub to " + gift.recipients[0] + "!";
			if (gift.cumulativeTotal > 1)
			{
				msg.text += " (" + std::to_string(gift.cumulativeTotal) + " total gifted)";
			}
		}
		else
		{
			msg.headerText = "Community Gift (" + gift.tier + ") • " + std::to_string(count) + " Subs";
			if (gift.cumulativeTotal > count)
			{
				msg.headerText += " • " + std::to_string(gift.cumulativeTotal) + " Total";
			}

			if (!gift.recipients.empty())
			{
				msg.text = gift.gifter + " gifted " + std::to_string(count) + " " + gift.tier +
					" subs to: " + JoinRecipients(gift.recipients) + "!";
			}
			else
			{
				msg.text = gift.gifter + " gifted " + std::to_string(count) + " " + gift.tier +
					" subs to the community!";
			}
		}

		msg.segments.push_back(TextSegment{ msg.text });
		std::cout << "[EVENTSUB] " << msg.headerText << " | " << msg.text << '\n';
		TwitchBot::SendAlert(msg);
	}

	void FlushPendingGift(const std::string& giftId)
	{
		PendingGift ready;
		{
			std::lock_guard<std::mutex> lock(pendingGiftMtx);
			auto it = pendingGifts.find(giftId);
			if (it == pendingGifts.end()) { return; }
			ready = std::move(it->second);
			pendingGifts.erase(it);
		}

		SendGiftCard(ready);
	}

	void ScheduleGiftFallback(const std::string& giftId)
	{
		if (!eventExecutor.has_value()) { return; }

		asio::co_spawn(*eventExecutor, [giftId]() -> awaitable<void> {
			asio::steady_timer timer(co_await asio::this_coro::executor, std::chrono::seconds(3));
			co_await timer.async_wait(asio::use_awaitable);
			FlushPendingGift(giftId);
		}, asio::detached);
	}
}

std::string Events::sessionId;

awaitable<void> Events::Run()
{
	auto executor = co_await asio::this_coro::executor;
	eventExecutor = executor;

	asio::ssl::context ctx(asio::ssl::context::tlsv12_client);
	ctx.load_verify_file("cacert.pem");
	ctx.set_verify_mode(asio::ssl::verify_peer);

	asio::ip::tcp::resolver resolver(executor);
	websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(executor, ctx);

	try
	{
		std::string host = "eventsub.wss.twitch.tv";
		auto const results = co_await resolver.async_resolve(host, "443", asio::use_awaitable);
		co_await beast::get_lowest_layer(ws).async_connect(results, asio::use_awaitable);

		if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str()))
		{
			throw boost::system::system_error(::ERR_get_error(), boost::asio::error::get_ssl_category());
		}

		co_await ws.next_layer().async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
		co_await ws.async_handshake(host, "/ws", asio::use_awaitable);

		std::cout << "[EVENTSUB] Secure WebSocket Connected.\n";

		beast::flat_buffer buffer;
		while (true)
		{
			co_await ws.async_read(buffer, asio::use_awaitable);

			std::string rawJson = beast::buffers_to_string(buffer.data());
			buffer.consume(buffer.size());

			ParseMessage(rawJson);
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "[EVENTSUB] Connection Error: " << e.what() << '\n';
	}
}

void Events::ParseMessage(const std::string& rawJson)
{
	try
	{
		nlohmann::json j = nlohmann::json::parse(rawJson);
		std::string messageType = j["metadata"]["message_type"];

		if (messageType == "session_welcome")
		{
			sessionId = j["payload"]["session"]["id"];
			std::cout << "[EVENTSUB] Connected! Session ID: " << sessionId << '\n';

			// --- SUBSCRIBE TO EVENTS HERE ---
			SubscribeToEvent("channel.follow", "2");
			SubscribeToEvent("channel.chat.notification", "1");
			SubscribeToEvent("channel.bits.use", "1");
			SubscribeToEvent("channel.raid", "1");
			SubscribeToEvent("channel.channel_points_custom_reward_redemption.add", "1");
			SubscribeToEvent("channel.hype_train.begin", "2");
			SubscribeToEvent("channel.hype_train.progress", "2");
			SubscribeToEvent("channel.hype_train.end", "2");
			SubscribeToEvent("channel.shoutout.create", "1");
		}
		else if (messageType == "session_keepalive") {}
		else if (messageType == "notification")
		{
			std::string subType = j["metadata"]["subscription_type"];
			JsonType& event = j["payload"]["event"];

			ChatMessage alertMsg;
			alertMsg.isAlert = true;

			if (subType == "channel.follow")
			{
				std::string userName = event["user_name"];
				alertMsg.text = userName + " followed the channel!";
				alertMsg.cardType = ChatCardType::Follow;
			}
			else if (subType == "channel.chat.notification")
			{
				ProcessChatNotification(event);
			}
			else if (subType == "channel.bits.use")
			{
				std::string userName = event.contains("user_name") && !event["user_name"].is_null()
					? event["user_name"].get<std::string>()
					: "Anonymous";

				int bits = event.contains("bits") ? event["bits"].get<int>() : 0;

				std::string powerUpType = "";
				if (event.contains("power_up_type") && !event["power_up_type"].is_null())
				{
					powerUpType = event["power_up_type"].get<std::string>();
				}

				if (powerUpType == "gigantify_an_emote")
				{
					alertMsg.text = userName + " gigantified an emote for " + std::to_string(bits) + " bits!";
				}
				else if (powerUpType == "message_effect")
				{
					alertMsg.text = userName + " used a message effect for " + std::to_string(bits) + " bits!";
				}
				else if (powerUpType == "celebration")
				{
					alertMsg.text = userName + " triggered a celebration for " + std::to_string(bits) + " bits!";
				}
				else
				{
					alertMsg.text = userName + " cheered " + std::to_string(bits) + " bits!";
				}

				alertMsg.cardType = ChatCardType::Bits;
			}
			else if (subType == "channel.raid")
			{
				std::string userName = event["from_broadcaster_user_name"].get<std::string>();
				int viewerCount = event["viewers"].get<int>();

				alertMsg.text = userName + " is raiding with a party of " + std::to_string(viewerCount) + "!";
				alertMsg.cardType = ChatCardType::Raid;

				TwitchBot::NotifyRaid(userName);
			}
			else if (subType == "channel.channel_points_custom_reward_redemption.add")
			{
				std::string userName = event["user_name"];
				std::string rewardTitle = event["reward"]["title"];

				if (rewardTitle == "Early Bird")
				{
					++TwitchBot::earlyBirdCount;

					alertMsg.userId = event.value("user_id", "");
					alertMsg.username = event.value("user_login", "");
					alertMsg.displayName = event.value("user_name", alertMsg.username);

					EarlyBirdStats& stats = Config::data.earlyBirds[alertMsg.userId];

					std::string placement;
					std::string count;

					if (TwitchBot::earlyBirdCount == 1)
					{
						placement = "1st";
						alertMsg.colorHex = "#d4af37";
						alertMsg.cardType = ChatCardType::EarlyBird1st;
						++stats.first;
						count = placement + " " + std::to_string(stats.first) + " Time" + (stats.first > 1 ? "s" : "");
					}
					else if (TwitchBot::earlyBirdCount == 2)
					{
						placement = "2nd";
						alertMsg.colorHex = "#c0c0c0";
						alertMsg.cardType = ChatCardType::EarlyBird2nd;
						++stats.second;
						count = placement + " " + std::to_string(stats.second) + " Time" + (stats.second > 1 ? "s" : "");
					}
					else if (TwitchBot::earlyBirdCount == 3)
					{
						placement = "3rd";
						alertMsg.colorHex = "#cd7f32";
						alertMsg.cardType = ChatCardType::EarlyBird3rd;
						++stats.third;
						count = placement + " " + std::to_string(stats.third) + " Time" + (stats.third > 1 ? "s" : "");
					}

					alertMsg.headerText = "EARLY BIRD!";
					alertMsg.text = alertMsg.displayName + " is the " + placement + " early bird to the stream! They've been " + count;

					TextSegment seg;
					seg.text = alertMsg.text;
					alertMsg.segments.push_back(seg);

					TwitchBot::SendAlert(alertMsg);
					Config::Save();
					return;
				}
				else
				{
					alertMsg.text = userName + " redeemed: " + rewardTitle;
					alertMsg.cardType = ChatCardType::Redemption;
				}
			}
			else if (subType == "channel.hype_train.begin")
			{
				TwitchBot::hypeTrain.isActive = true;
				
				int newLevel = event["level"].get<int>();
				int newProgress = event["progress"].get<int>();
				int newGoal = event["goal"].get<int>();

				if (newLevel > TwitchBot::hypeTrain.level)
				{
					TwitchBot::hypeTrain.level = newLevel;
					TwitchBot::hypeTrain.progress = newProgress;
					TwitchBot::hypeTrain.goal = newGoal;
				}
				else if (newLevel == TwitchBot::hypeTrain.level)
				{
					TwitchBot::hypeTrain.progress = std::max(newProgress, TwitchBot::hypeTrain.progress);
					TwitchBot::hypeTrain.goal = std::max(newGoal, TwitchBot::hypeTrain.goal);
				}

				//TODO:
				//for (auto& contribution : event["top_contributions"])
				//{
				//
				//}

				alertMsg.text = "A hype train has just started!";
				alertMsg.cardType = ChatCardType::HypeTrain;
			}
			else if (subType == "channel.hype_train.progress")
			{
				int newLevel = event["level"].get<int>();
				int newProgress = event["progress"].get<int>();
				int newGoal = event["goal"].get<int>();

				if (newLevel > TwitchBot::hypeTrain.level)
				{
					TwitchBot::hypeTrain.level = newLevel;
					TwitchBot::hypeTrain.progress = newProgress;
					TwitchBot::hypeTrain.goal = newGoal;
				}
				else if (newLevel == TwitchBot::hypeTrain.level)
				{
					TwitchBot::hypeTrain.progress = std::max(newProgress, TwitchBot::hypeTrain.progress);
					TwitchBot::hypeTrain.goal = std::max(newGoal, TwitchBot::hypeTrain.goal);
				}
			}
			else if (subType == "channel.hype_train.end")
			{
				TwitchBot::hypeTrain.isActive = false;
				TwitchBot::hypeTrain.level = 0;
				TwitchBot::hypeTrain.progress = 0;
				TwitchBot::hypeTrain.goal = 0;
			}
			else if (subType == "channel.shoutout.create")
			{
				std::string target = event["to_broadcaster_user_name"];

				alertMsg.text = "Shoutout to " + target + "! Go give them a follow!";
				alertMsg.cardType = ChatCardType::Shoutout;
			}

			if (!alertMsg.text.empty())
			{
				std::cout << "[EVENTSUB] " << alertMsg.text << '\n';
				alertMsg.segments.push_back(TextSegment{ alertMsg.text });
				TwitchBot::SendAlert(alertMsg);
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "[EVENTSUB] JSON Parse Error: " << e.what() << '\n';
	}
}

void Events::ProcessChatNotification(const JsonType& event)
{
	std::string noticeType = JsonStr(event, "notice_type");
	bool isAnon = JsonBool(event, "chatter_is_anonymous");
	std::string chatterName = isAnon ? "Anonymous" : JsonStr(event, "chatter_user_name", "Someone");

	ChatMessage msg{};
	msg.userId = JsonStr(event, "chatter_user_id");
	msg.username = JsonStr(event, "chatter_user_login");
	msg.displayName = chatterName;
	msg.colorHex = JsonStr(event, "color", "#9146FF");

	if (noticeType == "sub" && event.contains("sub") && event["sub"].is_object())
	{
		msg.cardType = ChatCardType::Sub;
		const auto& sub = event["sub"];
		std::string tier = FormatTier(sub);
		int duration = JsonInt(sub, "duration_months", 1);

		msg.headerText = "New Sub • " + tier;
		if (duration > 1)
		{
			msg.headerText += " (" + std::to_string(duration) + " Months)";
		}

		if (!PopulateMessageFragments(event, msg))
		{
			msg.isAlert = true;
			msg.text = chatterName + " just subscribed with " + tier + "!";
			msg.segments.push_back(TextSegment{ msg.text });
		}
	}
	else if (noticeType == "resub" && event.contains("resub") && event["resub"].is_object())
	{
		msg.cardType = ChatCardType::Sub;
		const auto& resub = event["resub"];
		std::string tier = FormatTier(resub);
		int cumulative = JsonInt(resub, "cumulative_months", 1);
		int streak = JsonInt(resub, "streak_months", 0);

		msg.headerText = "Resub (" + tier + ") • " + std::to_string(cumulative) +
			(cumulative == 1 ? " Month" : " Months");
		if (streak > 1)
		{
			msg.headerText += " • " + std::to_string(streak) + " Month Streak";
		}

		if (!PopulateMessageFragments(event, msg))
		{
			msg.isAlert = true;
			msg.text = chatterName + " resubscribed for " + std::to_string(cumulative) +
				(cumulative == 1 ? " month!" : " months!");
			msg.segments.push_back(TextSegment{ msg.text });
		}
	}
	else if (noticeType == "sub_gift" && event.contains("sub_gift") && event["sub_gift"].is_object())
	{
		const auto& gift = event["sub_gift"];
		std::string tier = FormatTier(gift);
		std::string recipient = JsonStr(gift, "recipient_user_name", "someone");
		std::string communityGiftId = JsonStr(gift, "community_gift_id");

		if (communityGiftId.empty())
		{
			PendingGift single{};
			single.gifter = chatterName;
			single.userId = msg.userId;
			single.username = msg.username;
			single.colorHex = msg.colorHex;
			single.tier = tier;
			single.expectedTotal = 1;
			single.cumulativeTotal = JsonInt(gift, "cumulative_total", 0);
			single.recipients.push_back(recipient);
			SendGiftCard(single);
			return;
		}

		bool shouldFlush = false;
		bool isNewEntry = false;
		{
			std::lock_guard<std::mutex> lock(pendingGiftMtx);
			auto [it, inserted] = pendingGifts.try_emplace(communityGiftId);
			isNewEntry = inserted;

			PendingGift& pending = it->second;
			if (pending.gifter.empty())
			{
				pending.gifter = chatterName;
				pending.userId = msg.userId;
				pending.username = msg.username;
				pending.colorHex = msg.colorHex;
				pending.tier = tier;
			}

			pending.recipients.push_back(recipient);

			if (pending.hasSummary && static_cast<int>(pending.recipients.size()) >= pending.expectedTotal)
			{
				shouldFlush = true;
			}
		}

		if (shouldFlush)
		{
			FlushPendingGift(communityGiftId);
		}
		else if (isNewEntry)
		{
			ScheduleGiftFallback(communityGiftId);
		}
		return;
	}
	else if (noticeType == "community_sub_gift" && event.contains("community_sub_gift") && event["community_sub_gift"].is_object())
	{
		const auto& gift = event["community_sub_gift"];
		std::string giftId = JsonStr(gift, "id");
		std::string tier = FormatTier(gift);
		int total = JsonInt(gift, "total", 1);
		int cumulative = JsonInt(gift, "cumulative_total", 0);

		if (giftId.empty())
		{
			PendingGift fallback{};
			fallback.gifter = chatterName;
			fallback.userId = msg.userId;
			fallback.username = msg.username;
			fallback.colorHex = msg.colorHex;
			fallback.tier = tier;
			fallback.expectedTotal = total;
			fallback.cumulativeTotal = cumulative;
			SendGiftCard(fallback);
			return;
		}

		bool shouldFlush = false;
		bool isNewEntry = false;
		{
			std::lock_guard<std::mutex> lock(pendingGiftMtx);
			auto [it, inserted] = pendingGifts.try_emplace(giftId);
			isNewEntry = inserted;

			PendingGift& pending = it->second;
			pending.gifter = chatterName;
			pending.userId = msg.userId;
			pending.username = msg.username;
			pending.colorHex = msg.colorHex;
			pending.tier = tier;
			pending.expectedTotal = total;
			pending.cumulativeTotal = cumulative;
			pending.hasSummary = true;

			if (static_cast<int>(pending.recipients.size()) >= pending.expectedTotal)
			{
				shouldFlush = true;
			}
		}

		if (shouldFlush)
		{
			FlushPendingGift(giftId);
		}
		else if (isNewEntry)
		{
			ScheduleGiftFallback(giftId);
		}
		return;
	}
	else if (noticeType == "gift_paid_upgrade" || noticeType == "prime_paid_upgrade")
	{
		msg.cardType = ChatCardType::Sub;
		msg.isAlert = true;
		msg.headerText = (noticeType == "prime_paid_upgrade") ? "Prime Upgrade" : "Gift Sub Upgrade";
		msg.text = chatterName + " upgraded their subscription!";
		msg.segments.push_back(TextSegment{ msg.text });
	}
	else if (noticeType == "watch_streak" && event.contains("watch_streak") && event["watch_streak"].is_object())
	{
		msg.cardType = ChatCardType::Sub;
		int streak = JsonInt(event["watch_streak"], "streak_count", 1);
		msg.headerText = "Watch Streak • " + std::to_string(streak) + " Streams";

		if (!PopulateMessageFragments(event, msg))
		{
			msg.isAlert = true;
			msg.text = chatterName + " is on a " + std::to_string(streak) + "-stream watch streak!";
			msg.segments.push_back(TextSegment{ msg.text });
		}
	}
	else
	{
		return;
	}

	std::cout << "[EVENTSUB] " << msg.headerText << " | " << msg.text << '\n';
	TwitchBot::SendAlert(msg);
}

void Events::SubscribeToEvent(const std::string& type, const std::string& version)
{
	nlohmann::json condition = {
		{ "broadcaster_user_id", OAuth::BroadcasterId() }
	};

	if (type == "channel.follow" || type == "channel.shoutout.create")
	{
		condition["moderator_user_id"] = OAuth::BroadcasterId();
	}
	else if (type == "channel.raid")
	{
		condition["to_broadcaster_user_id"] = OAuth::BroadcasterId();
	}
	else if (type == "channel.chat.notification")
	{
		condition["user_id"] = OAuth::BroadcasterId();
	}

	if (type == "channel.follow" ||
		type == "channel.subscribe" ||
		type == "channel.subscription.message" ||
		type == "channel.subscription.gift" ||
		type == "channel.channel_points_custom_reward_redemption.add")
	{
		condition["broadcaster_user_id"] = OAuth::BroadcasterId();
	}

	nlohmann::json payload = {
		{ "type", type },
		{ "version", version },
		{ "condition", condition },
		{ "transport",{
			{ "method", "websocket" },
			{ "session_id", sessionId }
		} }
	};

	std::thread([payload]() {
		cpr::Response r = cpr::Post(
			cpr::Url{ "https://api.twitch.tv/helix/eventsub/subscriptions" },
			cpr::Header{
				{ "Authorization", "Bearer " + OAuth::AccessToken() },
				{ "Client-Id", OAuth::ClientId() },
				{ "Content-Type", "application/json" }
			},
			cpr::Body{ payload.dump() }
		);

		if (r.status_code == 202)
		{
			std::cout << "[EVENTSUB] Successfully subscribed to: " << payload["type"] << '\n';
		}
		else
		{
			std::cerr << "[EVENTSUB] Failed to subscribe: " << r.text << '\n';
		}
	}).detach();
}

std::string Events::JsonStr(const nlohmann::json& j, const char* key, const std::string& fallback)
{
	if (j.contains(key) && j[key].is_string()) { return j[key].get<std::string>(); }
	return fallback;
}

int Events::JsonInt(const nlohmann::json& j, const char* key, int fallback)
{
	if (j.contains(key) && j[key].is_number_integer()) { return j[key].get<int>(); }
	return fallback;
}

bool Events::JsonBool(const nlohmann::json& j, const char* key, bool fallback)
{
	if (j.contains(key) && j[key].is_boolean()) { return j[key].get<bool>(); }
	return fallback;
}

std::string Events::FormatTier(const nlohmann::json& subObj)
{
	if (JsonBool(subObj, "is_prime")) { return "Prime"; }
	std::string raw = JsonStr(subObj, "sub_tier", "1000");
	if (raw == "2000") { return "Tier 2"; }
	if (raw == "3000") { return "Tier 3"; }
	return "Tier 1";
}

bool Events::HasVisibleText(std::string_view sv)
{
	return sv.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

bool Events::PopulateMessageFragments(const nlohmann::json& event, ChatMessage& msg)
{
	if (!event.contains("message") || !event["message"].is_object()) { return false; }

	const auto& m = event["message"];
	std::string rawText = JsonStr(m, "text");
	if (!HasVisibleText(rawText)) { return false; }

	msg.text = rawText;

	if (m.contains("fragments") && m["fragments"].is_array() && !m["fragments"].empty())
	{
		for (const auto& frag : m["fragments"])
		{
			std::string type = JsonStr(frag, "type", "text");
			if (type == "emote" && frag.contains("emote") && frag["emote"].is_object())
			{
				std::string emoteId = JsonStr(frag["emote"], "id");
				if (!emoteId.empty())
				{
					msg.segments.push_back(EmoteSegment{ emoteId });
					continue;
				}
			}

			std::string fragText = JsonStr(frag, "text");
			if (!fragText.empty())
			{
				msg.segments.push_back(TextSegment{ fragText });
			}
		}
	}
	else
	{
		msg.segments.push_back(TextSegment{ rawText });
	}

	return !msg.segments.empty();
}