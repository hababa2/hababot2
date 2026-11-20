#include "Debug.hpp"

#include "TwitchBot.hpp"
#include "Events.hpp"

#include "imgui.h"

#include <string>

void Debug::RenderDebugTools()
{
#if defined(_DEBUG)
	ImGui::BeginChild("DebugRegion", ImVec2(0, 80), false);

	if (ImGui::CollapsingHeader("Debug & Testing"))
	{
		if (ImGui::Button("Test Resub"))
		{
			std::string resubJson = R"({
				"metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.subscription.message"
			    },
			    "payload": {
			        "subscription": {
					    "id": "f1c2a387-161a-49f9-a165-0f21d7a4e1c4",
					    "type": "channel.subscription.message",
					    "version": "1",
					    "status": "enabled",
					    "cost": 0,
					    "condition": {
					       "broadcaster_user_id": "1337"
					    },
					     "transport": {
					        "method": "webhook",
					        "callback": "https://example.com/webhooks/callback"
					    },
					    "created_at": "2019-11-16T10:11:12.634234626Z"
					},
					"event": {
					    "user_id": "1234",
					    "user_login": "cool_user",
					    "user_name": "Cool_User",
					    "broadcaster_user_id": "1337",
					    "broadcaster_user_login": "cooler_user",
					    "broadcaster_user_name": "Cooler_User",
					    "tier": "1000",
					    "message": {
					        "text": "Love the stream! FevziGG",
					        "emotes": [
					            {
					                "begin": 17,
					                "end": 24,
					                "id": "302976485"
					            }
					        ]
					    },
					    "cumulative_months": 15,
					    "streak_months": 7,
					    "duration_months": 6
					}
			    }
			})";

			Events::ParseMessage(resubJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Test Gift Subs"))
		{
			std::string giftSubJson = R"({
				"metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.subscription.gift"
			    },
			    "payload": {
					"subscription": {
					    "id": "f1c2a387-161a-49f9-a165-0f21d7a4e1c4",
					    "type": "channel.subscription.gift",
					    "version": "1",
					    "status": "enabled",
					    "cost": 0,
					    "condition": {
					       "broadcaster_user_id": "1337"
					    },
					     "transport": {
					        "method": "webhook",
					        "callback": "https://example.com/webhooks/callback"
					    },
					    "created_at": "2019-11-16T10:11:12.634234626Z"
					},
					"event": {
					    "user_id": "1234",
					    "user_login": "cool_user",
					    "user_name": "Cool_User",
					    "broadcaster_user_id": "1337",
					    "broadcaster_user_login": "cooler_user",
					    "broadcaster_user_name": "Cooler_User",
					    "total": 2,
					    "tier": "1000",
					    "cumulative_total": 284,
					    "is_anonymous": false
					}
			    }
			})";

			Events::ParseMessage(giftSubJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Test Sub"))
		{
			std::string subJson = R"({
			    "metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.subscribe"
			    },
			    "payload": {
			        "event": {
			            "user_name": "MetalDrummer99",
			            "is_gift": false,
			            "tier": "1000"
			        }
			    }
			})";

			Events::ParseMessage(subJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Test Follow"))
		{
			std::string followJson = R"({
			    "metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.follow"
			    },
			    "payload": {
					"subscription": {
					    "id": "f1c2a387-161a-49f9-a165-0f21d7a4e1c4",
					    "type": "channel.follow",
					    "version": "2",
					    "status": "enabled",
					    "cost": 0,
					    "condition": {
					       "broadcaster_user_id": "1337",
					       "moderator_user_id": "1337"
					    },
					     "transport": {
					        "method": "webhook",
					        "callback": "https://example.com/webhooks/callback"
					    },
					    "created_at": "2019-11-16T10:11:12.634234626Z"
					},
					"event": {
					    "user_id": "1234",
					    "user_login": "cool_user",
					    "user_name": "Cool_User",
					    "broadcaster_user_id": "1337",
					    "broadcaster_user_login": "cooler_user",
					    "broadcaster_user_name": "Cooler_User",
					    "followed_at": "2020-07-15T18:16:11.17106713Z"
					}
			    }
			})";

			Events::ParseMessage(followJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Test First Chat"))
		{
			std::string_view fakeFirstIrc =
				"@badge-info=;badges=;color=#9146FF;display-name=NewViewer123;emotes=;first-msg=1;id=test-id-1;user-id=99999 "
				":newviewer123!newviewer123@newviewer123.tmi.twitch.tv PRIVMSG #channel :Hey everyone, first time catching a stream!\r\n";

			if (std::optional<ChatMessage> parsed = TwitchBot::ParseMsg(fakeFirstIrc))
			{
				TwitchBot::chatQueue.Push(std::move(parsed.value()));
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Test Shoutout"))
		{
			std::string shoutoutJson = R"({
			    "metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.shoutout.create"
			    },
			    "payload": {
					"subscription": {
						"id": "f1c2a387-161a-49f9-a165-0f21d7a4e1c4",
						"type": "channel.shoutout.create",
						"version": "1",
						"status": "enabled",
						"cost": 0,
						"condition": {
							"broadcaster_user_id": "12345",
							"moderator_user_id": "98765"
						},
						"transport": {
							"method": "webhook",
							"callback": "https://example.com/webhooks/callback"
						},
						"created_at": "2022-07-25T10:11:12.1236739Z"
					},
					"event": {
						"broadcaster_user_id": "12345",
						"broadcaster_user_name": "SimplySimple",
						"broadcaster_user_login": "simplysimple",
						"moderator_user_id": "98765",
						"moderator_user_name": "ParticularlyParticular123",
						"moderator_user_login": "particularlyparticular123",
						"to_broadcaster_user_id": "626262",
						"to_broadcaster_user_name": "SandySanderman",
						"to_broadcaster_user_login": "sandysanderman",
						"started_at": "2022-07-26T17:00:03.17106713Z",
						"viewer_count": 860,
						"cooldown_ends_at": "2022-07-26T17:02:03.17106713Z",
						"target_cooldown_ends_at":"2022-07-26T18:00:03.17106713Z"
					}
			    }
			})";

			Events::ParseMessage(shoutoutJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Test Raid"))
		{
			std::string raidJson = R"({
			    "metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.raid"
			    },
			    "payload": {
					"subscription": {
					    "id": "f1c2a387-161a-49f9-a165-0f21d7a4e1c4",
					    "type": "channel.raid",
					    "version": "1",
					    "status": "enabled",
					    "cost": 0,
					    "condition": {
					        "to_broadcaster_user_id": "1337"
					    },
					     "transport": {
					        "method": "webhook",
					        "callback": "https://example.com/webhooks/callback"
					    },
					    "created_at": "2019-11-16T10:11:12.634234626Z"
					},
					"event": {
					    "from_broadcaster_user_id": "1234",
					    "from_broadcaster_user_login": "cool_user",
					    "from_broadcaster_user_name": "Cool_User",
					    "to_broadcaster_user_id": "1337",
					    "to_broadcaster_user_login": "cooler_user",
					    "to_broadcaster_user_name": "Cooler_User",
					    "viewers": 9001
					}
			    }
			})";

			Events::ParseMessage(raidJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Test Early Bird"))
		{
			std::string earlyBirdJson = R"({
			    "metadata": {
			        "message_type": "notification",
			        "subscription_type": "channel.channel_points_custom_reward_redemption.add"
			    },
			    "payload": {
					"subscription": {
					    "id": "f1c2a387-161a-49f9-a165-0f21d7a4e1c4",
					    "type": "channel.channel_points_custom_reward_redemption.add",
					    "version": "1",
					    "status": "enabled",
					    "cost": 0,
					    "condition": {
					        "broadcaster_user_id": "1337",
					        "reward_id": "92af127c-7326-4483-a52b-b0da0be61c01"
					    },
					     "transport": {
					        "method": "webhook",
					        "callback": "https://example.com/webhooks/callback"
					    },
					    "created_at": "2019-11-16T10:11:12.634234626Z"
					},
					"event": {
					    "id": "17fa2df1-ad76-4804-bfa5-a40ef63efe63",
					    "broadcaster_user_id": "1337",
					    "broadcaster_user_login": "cool_user",
					    "broadcaster_user_name": "Cool_User",
					    "user_id": "9001",
					    "user_login": "cooler_user",
					    "user_name": "Cooler_User",
					    "user_input": "pogchamp",
					    "status": "unfulfilled",
					    "reward": {
					        "id": "92af127c-7326-4483-a52b-b0da0be61c01",
					        "title": "Early Bird",
					        "cost": 1,
					        "prompt": "reward prompt"
					    },
					    "redeemed_at": "2020-07-15T17:16:03.17106713Z"
					}
				}
			})";

			Events::ParseMessage(earlyBirdJson);
		}
	}

	ImGui::Separator();
	ImGui::EndChild();
#endif
}