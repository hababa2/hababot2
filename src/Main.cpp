#include "TwitchBot.hpp"
#include "Window.hpp"
#include "Events.hpp"
#include "SongQueue.hpp"

#include <boost/asio.hpp>

#include <thread>
#include <chrono>

namespace asio = boost::asio;

int main()
{
	if (!Window::Initialize()) { return 1; }

	asio::io_context ioc;
	asio::ssl::context ctx(asio::ssl::context::tls_client);
	ctx.set_verify_mode(asio::ssl::verify_none);

	TwitchBot::Initialize(ioc, ctx);

	asio::co_spawn(ioc, TwitchBot::Run(), asio::detached);
	asio::co_spawn(ioc, Events::Run(), asio::detached);
	std::thread networkThread([&ioc]() { ioc.run(); });

	while (Window::Update())
	{
		TwitchBot::Render();
		if (!Window::Resizing())
		{
			Window::Render();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	ioc.stop();
	if (networkThread.joinable()) { networkThread.join(); }

	TwitchBot::SaveEarlyBirdStats();
	Window::Shutdown();

	return 0;
}