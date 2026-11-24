#include "Events.hpp"

#include "OAuth.hpp"
#include "TwitchBot.hpp"
#include "Config.hpp"

#include <cpr/cpr.h>

#include <iostream>

std::string Events::sessionId;

awaitable<void> Events::Run()
{
	auto executor = co_await asio::this_coro::executor;

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
			SubscribeToEvent("channel.subscribe", "1");
			SubscribeToEvent("channel.subscription.message", "1");
			SubscribeToEvent("channel.subscription.gift", "1");
		}
		else if (messageType == "session_keepalive") {}
		else if (messageType == "notification")
		{
			std::string subType = j["metadata"]["subscription_type"];
			JsonType& event = j["payload"]["event"];

			ChatMessage alertMsg;
			alertMsg.isAlert = true;
			alertMsg.hasBackground = true;
			alertMsg.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

			if (subType == "channel.follow")
			{
				std::string userName = event["user_name"];
				alertMsg.text = userName + " followed the channel!";
				alertMsg.backgroundColor = ImVec4(0.73f, 0.96f, 0.94f, 0.60f);
			}
			else if (subType == "channel.subscribe" ||
				subType == "channel.subscription.message" ||
				subType == "channel.subscription.gift")
			{
				ProcessSubscription(subType, event);
			}
			else if (subType == "channel.chat.notification")
			{
				std::string noticeType = event["notice_type"];
				std::string chatterName = event["chatter_user_name"];

				if (noticeType == "sub_gift")
				{
					std::string recipient = event["sub_gift"]["recipient_user_name"];
					alertMsg.text = chatterName + " gifted a sub to " + recipient + "!";
					alertMsg.backgroundColor = ImVec4(0.8f, 0.2f, 0.6f, 1.0f);
				}
				else if (noticeType == "community_sub_gift")
				{
					int total = event["community_sub_gift"]["total"];
					alertMsg.text = chatterName + " gifted " + std::to_string(total) + " subs to the community!";
					alertMsg.backgroundColor = ImVec4(0.8f, 0.2f, 0.6f, 1.0f);
				}
				else if (noticeType == "sub" || noticeType == "resub")
				{
					alertMsg.text = chatterName + " just subscribed!";
					alertMsg.backgroundColor = ImVec4(0.6f, 0.2f, 0.8f, 1.0f);
				}
				else if (noticeType == "watch_streak")
				{
					int streak = event["watch_streak"]["streak_count"];
					alertMsg.text = chatterName + " is on a " + std::to_string(streak) + " stream watch streak!";
					alertMsg.backgroundColor = ImVec4(0.6f, 0.2f, 0.8f, 1.0f);
				}
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

				alertMsg.text = userName + " cheered " + std::to_string(bits) + " bits!";
				alertMsg.backgroundColor = ImVec4(0.8f, 0.5f, 0.1f, 1.0f);
			}
			else if (subType == "channel.raid")
			{
				std::string userName = event["from_broadcaster_user_name"].get<std::string>();
				int viewerCount = event["viewers"].get<int>();

				alertMsg.text = userName + " is raiding with a party of " + std::to_string(viewerCount) + "!";
				alertMsg.backgroundColor = ImVec4(0.8f, 0.5f, 0.1f, 0.9f);
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
						alertMsg.backgroundColor = ImVec4(0.83f, 0.68f, 0.21f, 0.65f);
						++stats.first;
						count = placement + " " + std::to_string(stats.first) + " Time" + (stats.first > 1 ? "s" : "");
					}
					else if (TwitchBot::earlyBirdCount == 2)
					{
						placement = "2nd";
						alertMsg.colorHex = "#c0c0c0";
						alertMsg.backgroundColor = ImVec4(0.77f, 0.77f, 0.77f, 0.65f);
						++stats.second;
						count = placement + " " + std::to_string(stats.second) + " Time" + (stats.second > 1 ? "s" : "");
					}
					else if (TwitchBot::earlyBirdCount == 3)
					{
						placement = "3rd";
						alertMsg.colorHex = "#cd7f32";
						alertMsg.backgroundColor = ImVec4(0.80f, 0.54f, 0.27f, 0.65f);
						++stats.third;
						count = placement + " " + std::to_string(stats.third) + " Time" + (stats.third > 1 ? "s" : "");
					}

					alertMsg.headerText = "EARLY BIRD!";
					alertMsg.headerColor = ImVec4(1.0f, 0.84f, 0.0f, 1.0f);
					alertMsg.hasBackground = true;
					alertMsg.hasAccentBar = true;
					alertMsg.accentColor = ImVec4(1.0f, 0.84f, 0.0f, 1.0f);

					alertMsg.text = alertMsg.displayName + " is the " + placement + " early bird to the stream! They've been " + count;

					TextSegment seg;
					seg.text = alertMsg.text;
					alertMsg.segments.push_back(seg);

					TwitchBot::SendAlert(alertMsg);
				}
				else
				{
					alertMsg.text = userName + " redeemed: " + rewardTitle;
					alertMsg.backgroundColor = ImVec4(0.1f, 0.6f, 0.4f, 1.0f);
				}
			}
			else if (subType == "channel.hype_train.begin")
			{
				int level = std::max(event["level"].get<int>(), TwitchBot::hypeTrain.level);

				TwitchBot::hypeTrain.isActive = true;
				TwitchBot::hypeTrain.progress = level > TwitchBot::hypeTrain.level ? event["progress"].get<int>() : std::max(event["progress"].get<int>(), TwitchBot::hypeTrain.level);
				TwitchBot::hypeTrain.level = level;
				TwitchBot::hypeTrain.goal = std::max(event["goal"].get<int>(), TwitchBot::hypeTrain.level);

				//TODO:
				//for (auto& contribution : event["top_contributions"])
				//{
				//
				//}

				alertMsg.text = "A hype train has just started!";
				alertMsg.backgroundColor = ImVec4(0.56f, 0.27f, 0.97f, 1.0f);
			}
			else if (subType == "channel.hype_train.progress")
			{
				int level = std::max(event["level"].get<int>(), TwitchBot::hypeTrain.level);

				TwitchBot::hypeTrain.progress = level > TwitchBot::hypeTrain.level ? event["progress"].get<int>() : std::max(event["progress"].get<int>(), TwitchBot::hypeTrain.level);
				TwitchBot::hypeTrain.level = level;
				TwitchBot::hypeTrain.goal = std::max(event["goal"].get<int>(), TwitchBot::hypeTrain.level);
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
				alertMsg.backgroundColor = ImVec4(0.9f, 0.4f, 0.1f, 0.75f);
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

void Events::ProcessSubscription(const std::string& subType, JsonType& event)
{
	ChatMessage subMsg{};
	subMsg.userId = event.value("user_id", "");
	subMsg.username = event.value("user_login", "");
	subMsg.displayName = event.value("user_name", subMsg.username);
	subMsg.colorHex = "#9146FF";

	subMsg.headerColor = ImVec4(0.85f, 0.45f, 0.95f, 1.0f);
	subMsg.hasBackground = true;
	subMsg.backgroundColor = ImVec4(0.35f, 0.15f, 0.55f, 0.65f);
	subMsg.hasAccentBar = true;
	subMsg.accentColor = ImVec4(0.65f, 0.35f, 0.95f, 1.0f);

	std::string tierRaw = event.value("tier", "1000");
	std::string tierText = (tierRaw == "1000") ? "Tier 1" : (tierRaw == "2000") ? "Tier 2" : (tierRaw == "3000") ? "Tier 3" : "Prime";

	if (subType == "channel.subscription.message")
	{
		int cumulative = event.value("cumulative_months", 1);
		int streak = event.value("streak_months", 0);

		subMsg.headerText = "Resub (" + tierText + "): " + std::to_string(cumulative) + " Months";

		if (streak > 1)
		{
			subMsg.headerText += " | " + std::to_string(streak) + " Month Streak!";
		}

		if (event.contains("message") && event["message"].contains("text"))
		{
			std::string rawText = event["message"]["text"].get<std::string>();
			subMsg.text = rawText;

			if (event["message"].contains("emotes") && event["message"]["emotes"].is_array() && !event["message"]["emotes"].empty())
			{
				int wLen = MultiByteToWideChar(CP_UTF8, 0, rawText.c_str(), -1, nullptr, 0);
				std::wstring wText(wLen, 0);
				MultiByteToWideChar(CP_UTF8, 0, rawText.c_str(), -1, &wText[0], wLen);
				if (!wText.empty() && wText.back() == L'\0') { wText.pop_back(); }

				struct EmoteLoc { size_t start, end; std::string id; };
				std::vector<EmoteLoc> emoteLocs;

				for (const auto& emoteNode : event["message"]["emotes"])
				{
					emoteLocs.push_back({
						emoteNode.value("begin", 0ULL),
						emoteNode.value("end", 0ULL),
						emoteNode.value("id", "")
						});
				}

				std::sort(emoteLocs.begin(), emoteLocs.end(), [](const auto& a, const auto& b) {
					return a.start < b.start;
				});

				size_t cursor = 0;
				for (const auto& eloc : emoteLocs)
				{
					if (eloc.start > cursor && eloc.start <= wText.length())
					{
						std::wstring wSub = wText.substr(cursor, eloc.start - cursor);
						int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wSub.c_str(), -1, nullptr, 0, nullptr, nullptr);
						std::string u8Sub(utf8Len, 0);
						WideCharToMultiByte(CP_UTF8, 0, wSub.c_str(), -1, &u8Sub[0], utf8Len, nullptr, nullptr);
						if (!u8Sub.empty() && u8Sub.back() == '\0') u8Sub.pop_back();

						if (!u8Sub.empty())
						{
							TextSegment seg;
							seg.text = u8Sub;
							subMsg.segments.push_back(seg);
						}
					}

					size_t emoteLen = (eloc.end >= eloc.start) ? (eloc.end - eloc.start + 1) : 0;
					std::wstring wEmote = wText.substr(eloc.start, emoteLen);

					int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wEmote.c_str(), -1, nullptr, 0, nullptr, nullptr);
					std::string u8Emote(utf8Len, 0);
					WideCharToMultiByte(CP_UTF8, 0, wEmote.c_str(), -1, &u8Emote[0], utf8Len, nullptr, nullptr);
					if (!u8Emote.empty() && u8Emote.back() == '\0') u8Emote.pop_back();

					EmoteSegment emoteSeg;
					emoteSeg.emoteId = eloc.id;
					subMsg.segments.push_back(emoteSeg);

					cursor = eloc.end + 1;
				}

				if (cursor < wText.length())
				{
					std::wstring wSub = wText.substr(cursor);
					int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wSub.c_str(), -1, nullptr, 0, nullptr, nullptr);
					std::string u8Sub(utf8Len, 0);
					WideCharToMultiByte(CP_UTF8, 0, wSub.c_str(), -1, &u8Sub[0], utf8Len, nullptr, nullptr);
					if (!u8Sub.empty() && u8Sub.back() == '\0') u8Sub.pop_back();

					if (!u8Sub.empty())
					{
						TextSegment seg;
						seg.text = u8Sub;
						subMsg.segments.push_back(seg);
					}
				}
			}
			else
			{
				TextSegment seg;
				seg.text = rawText;
				subMsg.segments.push_back(seg);
			}
		}
		else
		{
			subMsg.text = "Just resubscribed!";

			TextSegment seg;
			seg.text = subMsg.text;
			subMsg.segments.push_back(seg);
			subMsg.isAlert = true;
		}
	}
	else if (subType == "channel.subscription.gift")
	{
		int total = event.value("total", 1);
		int cumulative = event.value("cumulative_total", 1);

		subMsg.text = subMsg.displayName + " gifted " + std::to_string(total) + " " + tierText + " subs to the community! " + std::to_string(cumulative) + " total!";
		subMsg.segments.push_back(TextSegment{ subMsg.text });
		subMsg.isAlert = true;
	}

	TwitchBot::SendAlert(subMsg);
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