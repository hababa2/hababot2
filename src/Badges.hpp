#pragma once

#include <unordered_map>
#include <string>
#include <mutex>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

class Badges
{
public:
	static void Initialize(ID3D11Device* device);

	static void LoadBadgeMappings(const std::string& accessToken, const std::string& clientId, const std::string& channelName);
	static ID3D11ShaderResourceView* GetBadge(const std::string& badgeKey);
	static bool LoadTextureFromMemory(const unsigned char* buffer, size_t bufferLength, ID3D11Device* d3dDevice, ID3D11ShaderResourceView** outSrv);

private:
	static std::unordered_map<std::string, ID3D11ShaderResourceView*> cache;
	static std::unordered_map<std::string, std::string> urlMap;
	static std::mutex mutex;
	static ID3D11Device* device;
};