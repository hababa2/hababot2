#pragma once

#include "json.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

using JsonType = nlohmann::json_abi_v3_12_0::basic_json<>::value_type;

class Events
{
public:
    static boost::asio::awaitable<void> Run();
    static void ParseMessage(const std::string& rawJson);

private:
    static void SubscribeToEvent(const std::string& type, const std::string& version);
    static void ProcessSubscription(const std::string& subType, JsonType& event);

    static std::string sessionId;
};