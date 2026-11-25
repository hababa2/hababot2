#pragma once

#include "json.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

using JsonType = nlohmann::json_abi_v3_12_0::basic_json<>::value_type;

struct ChatMessage;

class Events
{
public:
    static boost::asio::awaitable<void> Run();
    static void ParseMessage(const std::string& rawJson);

private:
    static void SubscribeToEvent(const std::string& type, const std::string& version);
    static void ProcessChatNotification(const JsonType& event);

	static std::string JsonStr(const nlohmann::json& j, const char* key, const std::string& fallback = "");
	static int JsonInt(const nlohmann::json& j, const char* key, int fallback = 0);
	static bool JsonBool(const nlohmann::json& j, const char* key, bool fallback = false);
	static std::string FormatTier(const nlohmann::json& subObj);
	static bool HasVisibleText(std::string_view sv);
	static bool PopulateMessageFragments(const nlohmann::json& event, ChatMessage& msg);

    static std::string sessionId;
};