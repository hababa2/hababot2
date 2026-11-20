#include "Badges.hpp"

#include "OAuth.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "json.hpp"

#include <cpr/cpr.h>

#include <d3d11.h>

#include <thread>
#include <iostream>

std::unordered_map<std::string, ID3D11ShaderResourceView*> Badges::cache;
std::unordered_map<std::string, std::string> Badges::urlMap;
std::mutex Badges::mutex;
ID3D11Device* Badges::device;

void Badges::Initialize(ID3D11Device* device_)
{
	device = device_;
}

void Badges::LoadBadgeMappings(const std::string& accessToken, const std::string& clientId, const std::string& channelName)
{
	cpr::Header helixHeaders{
		{"Authorization", "Bearer " + accessToken},
		{"Client-Id", clientId}
	};

	auto FetchEndpoint = [&](const std::string& url) {
		cpr::Response r = cpr::Get(cpr::Url{ url }, helixHeaders);

		if (r.status_code == 200)
		{
			try
			{
				nlohmann::json j = nlohmann::json::parse(r.text);
				for (const auto& set : j["data"])
				{
					std::string setId = set["set_id"];
					for (const auto& version : set["versions"])
					{
						std::string versionId = version["id"];
						urlMap[setId + "/" + versionId] = version["image_url_1x"];
					}
				}
			}
			catch (...) {}
		}
	};

	FetchEndpoint("https://api.twitch.tv/helix/chat/badges/global");

	if (!OAuth::BroadcasterId().empty())
	{
		FetchEndpoint("https://api.twitch.tv/helix/chat/badges?broadcaster_id=" + OAuth::BroadcasterId());
		std::cout << "[SYSTEM] Badge mappings loaded successfully for ID: " << OAuth::BroadcasterId() << '\n';
	}
	else
	{
		std::cout << "[SYSTEM] Global badges loaded. Skipped channel badges (No ID).\n";
	}
}

ID3D11ShaderResourceView* Badges::GetBadge(const std::string& badgeKey)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (cache.contains(badgeKey))
	{
		return cache[badgeKey];
	}

	if (urlMap.contains(badgeKey))
	{
		cache[badgeKey] = nullptr;

		std::string url = urlMap[badgeKey];

		std::thread([badgeKey, url]() {
			cpr::Response r = cpr::Get(cpr::Url{ url });
			if (r.status_code == 200)
			{
				ID3D11ShaderResourceView* srv = nullptr;
				if (LoadTextureFromMemory((const unsigned char*)r.text.data(), r.text.size(), device, &srv))
				{
					std::lock_guard<std::mutex> innerLock(mutex);
					cache[badgeKey] = srv;
				}
			}
		}).detach();
	}

	return nullptr;
}

bool Badges::LoadTextureFromMemory(const unsigned char* buffer, size_t bufferLength, ID3D11Device* d3dDevice, ID3D11ShaderResourceView** outSrv)
{
	int imageWidth = 0, imageHeight = 0, channels = 0;

	unsigned char* imageData = stbi_load_from_memory(buffer, (int)bufferLength, &imageWidth, &imageHeight, &channels, 4);
	if (imageData == nullptr) return false;

	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = imageWidth;
	desc.Height = imageHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = imageData;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;

	ID3D11Texture2D* pTexture = nullptr;
	HRESULT hr = d3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
	stbi_image_free(imageData);

	if (hr != S_OK) { return false; }

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;

	hr = d3dDevice->CreateShaderResourceView(pTexture, &srvDesc, outSrv);
	pTexture->Release();

	return (hr == S_OK);
}