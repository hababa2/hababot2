#pragma once

#include <unordered_map>
#include <string>
#include <mutex>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

struct EmoteTexture
{
	std::vector<ID3D11ShaderResourceView*> frames;
	std::vector<int> delays;
	int totalDurationMs = 0;
	bool isAnimated = false;
};

class Emotes
{
public:
	static void Initialize(ID3D11Device* device);

	static ID3D11ShaderResourceView* GetEmote(const std::string& emoteId, bool isJumbo = false);
	static bool LoadEmoteFromMemory(const unsigned char* buffer, size_t bufferLength, ID3D11Device* d3dDevice, EmoteTexture& outEmote);

private:
	static std::unordered_map<std::string, EmoteTexture> cache;
	static std::mutex mutex;
	static ID3D11Device* device;
};