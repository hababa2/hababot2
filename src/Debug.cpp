#include "Debug.hpp"

#include "TwitchBot.hpp"
#include "Events.hpp"

#include "imgui.h"

#include <string>
#include <vector>

void Debug::RenderDebugTools()
{
#if defined(_DEBUG)
	static int debugGiftCounter = 1;

	ImGui::BeginChild("DebugRegion", ImVec2(0, 100), false);

	if (ImGui::CollapsingHeader("Debug & Testing"))
	{
		if (ImGui::Button("Sub (Blank)"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "sub",
						"chatter_is_anonymous": false,
						"chatter_user_id": "1001",
						"chatter_user_login": "metaldrummer99",
						"chatter_user_name": "MetalDrummer99",
						"color": "#9146FF",
						"message": { "text": "", "fragments": [] },
						"sub": {
							"sub_tier": "1000",
							"is_prime": false,
							"duration_months": 1
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}

		ImGui::SameLine();

		if (ImGui::Button("Resub (Msg)"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "resub",
						"chatter_is_anonymous": false,
						"chatter_user_id": "1234",
						"chatter_user_login": "cool_user",
						"chatter_user_name": "Cool_User",
						"color": "#9146FF",
						"message": {
							"text": "Love the stream! FevziGG",
							"fragments": [
								{ "type": "text", "text": "Love the stream! " },
								{ "type": "emote", "text": "FevziGG", "emote": { "id": "302976485" } }
							]
						},
						"resub": {
							"sub_tier": "1000",
							"is_prime": false,
							"cumulative_months": 15,
							"streak_months": 7
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}

		ImGui::SameLine();

		if (ImGui::Button("Resub (Blank)"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "resub",
						"chatter_is_anonymous": false,
						"chatter_user_id": "1235",
						"chatter_user_login": "quiet_sub",
						"chatter_user_name": "QuietSub",
						"color": "#9146FF",
						"message": { "text": "", "fragments": [] },
						"resub": {
							"sub_tier": "2000",
							"is_prime": false,
							"cumulative_months": 6,
							"streak_months": null
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}

		ImGui::SameLine();

		if (ImGui::Button("Gift (Single)"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "sub_gift",
						"chatter_is_anonymous": false,
						"chatter_user_id": "2001",
						"chatter_user_login": "generous_viewer",
						"chatter_user_name": "GenerousViewer",
						"color": "#FF69B4",
						"sub_gift": {
							"sub_tier": "1000",
							"recipient_user_name": "LuckyChatter",
							"community_gift_id": null,
							"cumulative_total": 12
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}

		ImGui::SameLine();

		if (ImGui::Button("Gift (1 Random)"))
		{
			std::string giftId = "debug-rand-" + std::to_string(debugGiftCounter++);

			std::string summaryJson = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "community_sub_gift",
						"chatter_is_anonymous": false,
						"chatter_user_id": "2002",
						"chatter_user_login": "random_gifter",
						"chatter_user_name": "RandomGifter",
						"color": "#FF69B4",
						"community_sub_gift": {
							"id": ")" + giftId + R"(",
							"sub_tier": "1000",
							"total": 1,
							"cumulative_total": 5
						}
					}
				}
			})";

			std::string recipientJson = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "sub_gift",
						"chatter_is_anonymous": false,
						"chatter_user_id": "2002",
						"chatter_user_login": "random_gifter",
						"chatter_user_name": "RandomGifter",
						"color": "#FF69B4",
						"sub_gift": {
							"sub_tier": "1000",
							"recipient_user_name": "RandomWinner",
							"community_gift_id": ")" + giftId + R"(",
							"cumulative_total": 5
						}
					}
				}
			})";

			Events::ParseMessage(summaryJson);
			Events::ParseMessage(recipientJson);
		}

		ImGui::SameLine();

		if (ImGui::Button("Gift Bomb (5)"))
		{
			std::string giftId = "debug-bomb-" + std::to_string(debugGiftCounter++);

			std::string summaryJson = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "community_sub_gift",
						"chatter_is_anonymous": false,
						"chatter_user_id": "2003",
						"chatter_user_login": "mega_gifter",
						"chatter_user_name": "MegaGifter",
						"color": "#FF69B4",
						"community_sub_gift": {
							"id": ")" + giftId + R"(",
							"sub_tier": "1000",
							"total": 5,
							"cumulative_total": 284
						}
					}
				}
			})";

			Events::ParseMessage(summaryJson);

			const std::vector<std::string> recipients = {
				"ViewerOne", "ViewerTwo", "ViewerThree", "ViewerFour", "ViewerFive"
			};

			for (const std::string& rec : recipients)
			{
				std::string recJson = R"({
					"metadata": {
						"message_type": "notification",
						"subscription_type": "channel.chat.notification"
					},
					"payload": {
						"event": {
							"notice_type": "sub_gift",
							"chatter_is_anonymous": false,
							"chatter_user_id": "2003",
							"chatter_user_login": "mega_gifter",
							"chatter_user_name": "MegaGifter",
							"color": "#FF69B4",
							"sub_gift": {
								"sub_tier": "1000",
								"recipient_user_name": ")" + rec + R"(",
								"community_gift_id": ")" + giftId + R"(",
								"cumulative_total": 284
							}
						}
					}
				})";

				Events::ParseMessage(recJson);
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Gift Bomb (50)"))
		{
			std::string giftId = "debug-bomb-" + std::to_string(debugGiftCounter++);

			std::string summaryJson = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "community_sub_gift",
						"chatter_is_anonymous": false,
						"chatter_user_id": "2003",
						"chatter_user_login": "mega_gifter",
						"chatter_user_name": "MegaGifter",
						"color": "#FF69B4",
						"community_sub_gift": {
							"id": ")" + giftId + R"(",
							"sub_tier": "1000",
							"total": 50,
							"cumulative_total": 284
						}
					}
				}
			})";

			Events::ParseMessage(summaryJson);

			const std::vector<std::string> recipients(50, "Viewer");

			for (const std::string& rec : recipients)
			{
				std::string recJson = R"({
					"metadata": {
						"message_type": "notification",
						"subscription_type": "channel.chat.notification"
					},
					"payload": {
						"event": {
							"notice_type": "sub_gift",
							"chatter_is_anonymous": false,
							"chatter_user_id": "2003",
							"chatter_user_login": "mega_gifter",
							"chatter_user_name": "MegaGifter",
							"color": "#FF69B4",
							"sub_gift": {
								"sub_tier": "1000",
								"recipient_user_name": ")" + rec + R"(",
								"community_gift_id": ")" + giftId + R"(",
								"cumulative_total": 284
							}
						}
					}
				})";

				Events::ParseMessage(recJson);
			}
		}

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

		ImGui::SameLine();

		if (ImGui::Button("Watch Streak (Msg)"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "watch_streak",
						"chatter_is_anonymous": false,
						"chatter_user_id": "3001",
						"chatter_user_login": "loyal_viewer",
						"chatter_user_name": "LoyalViewer",
						"color": "#00CED1",
						"message": {
							"text": "Never miss a stream! FevziGG",
							"fragments": [
								{ "type": "text", "text": "Never miss a stream! " },
								{ "type": "emote", "text": "FevziGG", "emote": { "id": "302976485" } }
							]
						},
						"watch_streak": {
							"streak_count": 5,
							"channel_points_awarded": 450
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}

		ImGui::SameLine();

		if (ImGui::Button("Watch Streak (Blank)"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "watch_streak",
						"chatter_is_anonymous": false,
						"chatter_user_id": "3002",
						"chatter_user_login": "lurker_99",
						"chatter_user_name": "Lurker99",
						"color": "#9146FF",
						"message": { "text": "", "fragments": [] },
						"watch_streak": {
							"streak_count": 10,
							"channel_points_awarded": 450
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}

		ImGui::SameLine();

		if (ImGui::Button("Prime Upgrade"))
		{
			std::string json = R"({
				"metadata": {
					"message_type": "notification",
					"subscription_type": "channel.chat.notification"
				},
				"payload": {
					"event": {
						"notice_type": "prime_paid_upgrade",
						"chatter_is_anonymous": false,
						"chatter_user_id": "3003",
						"chatter_user_login": "prime_upgrader",
						"chatter_user_name": "PrimeUpgrader",
						"color": "#1E90FF",
						"message": { "text": "", "fragments": [] },
						"prime_paid_upgrade": {
							"sub_tier": "1000"
						}
					}
				}
			})";

			Events::ParseMessage(json);
		}
	}

	ImGui::Separator();
	ImGui::EndChild();
#endif
}