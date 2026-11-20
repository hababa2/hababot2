#pragma once

#include <string>
#include <optional>

class OAuth
{
public:
	static bool Authenticate();

	static const std::string& BroadcasterId();
	static const std::string& ClientId();
	static const std::string& AccessToken();
	static const std::string& ChannelName();

private:
	static bool SaveTokens(const std::string& filepath);
	static void LoadTokens(const std::string& filepath);
	static std::optional<std::string> CatchOAuthToken();
	static bool ValidateAndExtractUserData();

	static bool Valid();

	static std::string channelName;
	static std::string broadcasterId;
	static std::string clientId;
	static std::string accessToken;
};