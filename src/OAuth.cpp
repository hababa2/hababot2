#include "OAuth.hpp"
#include "Badges.hpp"
#include "Config.hpp"

#include "json.hpp"

#include <cpr/cpr.h>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <iostream>
#include <fstream>

using json = nlohmann::json;

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

std::string OAuth::broadcasterId;
std::string OAuth::clientId;
std::string OAuth::accessToken;
std::string OAuth::channelName;

bool OAuth::Authenticate()
{
	clientId = "or5qfnj2finuzd3120a42rlfx9tcyc";

	bool readyToConnect = false;

	if (Valid())
	{
		if (ValidateAndExtractUserData())
		{
			readyToConnect = true;
			std::cout << "[SYSTEM] Saved access token is valid for channel: " << channelName << '\n';
		}
		else
		{
			std::cout << "[SYSTEM] Saved access token expired or invalid.\n";
			Config::data.oauthAccessToken.clear();
			Config::Save();
		}
	}

	if (!readyToConnect)
	{
		std::cout << "[SYSTEM] Full browser login required. Opening browser...\n";
		std::optional<std::string> newToken = CatchOAuthToken();

		if (newToken)
		{
			Config::data.oauthAccessToken = newToken.value();
			Config::Save();

			if (ValidateAndExtractUserData())
			{
				readyToConnect = true;
				std::cout << "[SYSTEM] Authentication successful for channel: " << channelName << '\n';
			}
		}
	}

	if (readyToConnect)
	{
		Badges::LoadBadgeMappings(Config::data.oauthAccessToken, clientId, channelName);
	}

	return readyToConnect;
}

bool OAuth::ValidateAndExtractUserData()
{
	cpr::Response r = cpr::Get(
		cpr::Url{ "https://id.twitch.tv/oauth2/validate" },
		cpr::Header{ { "Authorization", "OAuth " + Config::data.oauthAccessToken } }
	);

	if (r.status_code == 200)
	{
		try
		{
			json j = json::parse(r.text);
			channelName = j.value("login", "");
			broadcasterId = j.value("user_id", "");
			return !channelName.empty() && !broadcasterId.empty();
		}
		catch (...)
		{
			return false;
		}
	}
	return false;
}

std::optional<std::string> OAuth::CatchOAuthToken()
{
	try
	{
		net::io_context ioc;
		tcp::acceptor acceptor(ioc, { net::ip::make_address("127.0.0.1"), 3000 });

		std::string redirectUri = "http://localhost:3000/";
		std::string scopes =
			"bits:read+"
			"chat:read+"
			"chat:edit+"
			"user:bot+"
			"user:read:chat+"
			"channel:read:hype_train+"
			"channel:read:redemptions+"
			"channel:read:subscriptions+"
			"moderator:read:followers+"
			"moderator:read:shoutouts+"
			"moderator:manage:banned_users+"
			"moderator:manage:chat_messages";

		std::string authUrl = "https://id.twitch.tv/oauth2/authorize?client_id=" + clientId +
			"&redirect_uri=" + redirectUri +
			"&response_type=token&scope=" + scopes;

		ShellExecuteA(0, 0, authUrl.c_str(), 0, 0, SW_SHOW);
		std::cout << "[OAuth] Waiting for Twitch authentication in browser...\n";

		std::optional<std::string> capturedToken;

		while (!capturedToken.has_value())
		{
			tcp::socket socket(ioc);
			acceptor.accept(socket);

			beast::flat_buffer buffer;
			http::request<http::string_body> req;
			beast::error_code ec;
			http::read(socket, buffer, req, ec);

			if (ec) continue;

			http::response<http::string_body> res{ http::status::ok, req.version() };
			res.set(http::field::server, "TwitchBot-OAuth");
			res.keep_alive(false);

			if (req.method() == http::verb::get && req.target() == "/")
			{
				res.set(http::field::content_type, "text/html");
				res.body() = R"(
					<!DOCTYPE html>
					<html><head><title>Twitch Login Success</title>
					<style>body{background:#18181B;color:#fff;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;}</style>
					</head><body>
					<div id="msg"><h2>Logging you in...</h2></div>
					<script>
						const hash = window.location.hash.substring(1);
						const params = new URLSearchParams(hash);
						const token = params.get('access_token');
						if (token) {
							fetch('/token', { method: 'POST', body: token }).then(() => {
								document.getElementById('msg').innerHTML = "<h2>Success! You can close this tab.</h2>";
							});
						} else {
							document.getElementById('msg').innerHTML = "<h2>Authentication failed. No token found.</h2>";
						}
					</script>
					</body></html>
				)";
			}
			else if (req.method() == http::verb::post && req.target() == "/token")
			{
				capturedToken = req.body();
				res.set(http::field::content_type, "text/plain");
				res.body() = "ok";
			}
			else
			{
				res.result(http::status::not_found);
			}

			res.prepare_payload();
			http::write(socket, res, ec);
			socket.shutdown(tcp::socket::shutdown_send, ec);
		}

		return capturedToken;
	}
	catch (const std::exception& e)
	{
		std::cerr << "[OAuth] Local Server Error: " << e.what() << '\n';
		return std::nullopt;
	}
}

bool OAuth::Valid() { return !Config::data.oauthAccessToken.empty(); }
const std::string& OAuth::ChannelName() { return channelName; }
const std::string& OAuth::BroadcasterId() { return broadcasterId; }
const std::string& OAuth::ClientId() { return clientId; }
const std::string& OAuth::AccessToken() { return Config::data.oauthAccessToken; }