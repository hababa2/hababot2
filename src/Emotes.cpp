#include "Emotes.hpp"

#include "OAuth.hpp"

#include "json.hpp"

#include "imgui.h"

#include <cpr/cpr.h>

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <thread>
#include <iostream>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

std::unordered_map<std::string, EmoteTexture> Emotes::cache;
std::mutex Emotes::mutex;
ID3D11Device* Emotes::device;

void Emotes::Initialize(ID3D11Device* device_)
{
	device = device_;
}

ID3D11ShaderResourceView* Emotes::GetEmote(const std::string& emoteId, bool isJumbo)
{
	std::lock_guard<std::mutex> lock(mutex);

	std::string cacheKey = isJumbo ? (emoteId + "_jumbo") : emoteId;

	if (cache.contains(cacheKey))
	{
		EmoteTexture& emote = cache[cacheKey];

		if (emote.frames.empty()) { return nullptr; }

		if (!emote.isAnimated) { return emote.frames[0]; }

		int currentTimeMs = (int)(ImGui::GetTime() * 1000.0) % emote.totalDurationMs;

		int accumulatedTime = 0;
		for (size_t i = 0; i < emote.frames.size(); ++i)
		{
			accumulatedTime += emote.delays[i];
			if (currentTimeMs < accumulatedTime)
			{
				return emote.frames[i];
			}
		}
		return emote.frames.back();
	}

	cache[cacheKey] = EmoteTexture();

	std::string url;
	if (emoteId.starts_with("http"))
	{
		url = emoteId;
	}
	else if (emoteId.starts_with("emoji_"))
	{
		std::string hexCode = emoteId.substr(6);
		url = "https://cdnjs.cloudflare.com/ajax/libs/twemoji/14.0.2/72x72/" + hexCode + ".png";
	}
	else
	{
		url = "https://static-cdn.jtvnw.net/emoticons/v2/" + emoteId + "/default/dark/" + (isJumbo ? "3.0" : "2.0");
	}

	std::thread([cacheKey, url]() {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		cpr::Response r = cpr::Get(cpr::Url{ url });
		if (r.status_code == 200)
		{
			EmoteTexture newEmote;
			if (LoadEmoteFromMemory((const unsigned char*)r.text.data(), r.text.size(), device, newEmote))
			{
				std::lock_guard<std::mutex> innerLock(mutex);
				cache[cacheKey] = newEmote;
			}
		}

		CoUninitialize();
	}).detach();

	return nullptr;
}

bool Emotes::LoadEmoteFromMemory(const unsigned char* buffer, size_t bufferLength, ID3D11Device* d3dDevice, EmoteTexture& outEmote)
{
	HRESULT hr;

	ComPtr<IWICImagingFactory> factory;
	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
	if (FAILED(hr)) { return false; }

	ComPtr<IWICStream> stream;
	hr = factory->CreateStream(&stream);
	if (FAILED(hr)) { return false; }

	hr = stream->InitializeFromMemory(const_cast<BYTE*>(buffer), static_cast<DWORD>(bufferLength));
	if (FAILED(hr)) { return false; }

	ComPtr<IWICBitmapDecoder> decoder;
	hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
	if (FAILED(hr)) { return false; }

	UINT frameCount = 0;
	decoder->GetFrameCount(&frameCount);
	if (frameCount == 0) { return false; }
	outEmote.isAnimated = (frameCount > 1);

	UINT logicalWidth = 0, logicalHeight = 0;
	ComPtr<IWICMetadataQueryReader> decoderMetadata;
	if (SUCCEEDED(decoder->GetMetadataQueryReader(&decoderMetadata)))
	{
		PROPVARIANT propValue;
		PropVariantInit(&propValue);
		if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Width", &propValue)) && propValue.vt == VT_UI2) { logicalWidth = propValue.uiVal; }
		PropVariantClear(&propValue);

		if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Height", &propValue)) && propValue.vt == VT_UI2) { logicalHeight = propValue.uiVal; }
		PropVariantClear(&propValue);
	}

	if (logicalWidth == 0 || logicalHeight == 0)
	{
		ComPtr<IWICBitmapFrameDecode> firstFrame;
		if (SUCCEEDED(decoder->GetFrame(0, &firstFrame))) { firstFrame->GetSize(&logicalWidth, &logicalHeight); }
	}

	if (logicalWidth == 0 || logicalHeight == 0) { return false; }

	constexpr UINT MinStandardCanvas = 56;
	UINT canvasSize = max(MinStandardCanvas, max(logicalWidth, logicalHeight));
	UINT canvasStride = canvasSize * 4;
	UINT canvasBufferSize = canvasStride * canvasSize;

	UINT baseOffsetX = (canvasSize - logicalWidth) / 2;
	UINT baseOffsetY = (canvasSize - logicalHeight) / 2;

	std::vector<BYTE> canvas(canvasBufferSize, 0);
	std::vector<BYTE> previousCanvas;

	for (UINT i = 0; i < frameCount; ++i)
	{
		ComPtr<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(i, &frame))) { continue; }

		int frameDelayMs = 33;
		UINT frameLeft = 0, frameTop = 0;
		BYTE disposal = 0;

		ComPtr<IWICMetadataQueryReader> frameMetadata;
		if (SUCCEEDED(frame->GetMetadataQueryReader(&frameMetadata)))
		{
			PROPVARIANT propValue;
			PropVariantInit(&propValue);

			if (SUCCEEDED(frameMetadata->GetMetadataByName(L"/grctlext/Delay", &propValue)))
			{
				if (propValue.vt == VT_UI2 && propValue.uiVal > 0)
					frameDelayMs = propValue.uiVal * 10;
				PropVariantClear(&propValue);
			}
			if (SUCCEEDED(frameMetadata->GetMetadataByName(L"/imgdesc/Left", &propValue)))
			{
				if (propValue.vt == VT_UI2) frameLeft = propValue.uiVal;
				PropVariantClear(&propValue);
			}
			if (SUCCEEDED(frameMetadata->GetMetadataByName(L"/imgdesc/Top", &propValue)))
			{
				if (propValue.vt == VT_UI2) frameTop = propValue.uiVal;
				PropVariantClear(&propValue);
			}
			if (SUCCEEDED(frameMetadata->GetMetadataByName(L"/grctlext/Disposal", &propValue)))
			{
				if (propValue.vt == VT_UI1) disposal = propValue.bVal;
				PropVariantClear(&propValue);
			}
		}

		outEmote.delays.push_back(frameDelayMs);
		outEmote.totalDurationMs += frameDelayMs;

		if (disposal == 3)
		{
			previousCanvas = canvas;
		}

		ComPtr<IWICFormatConverter> converter;
		if (FAILED(factory->CreateFormatConverter(&converter))) { continue; }
		if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) { continue; }

		UINT frameWidth = 0, frameHeight = 0;
		converter->GetSize(&frameWidth, &frameHeight);

		UINT frameStride = frameWidth * 4;
		std::vector<BYTE> framePixels(frameStride * frameHeight);
		if (FAILED(converter->CopyPixels(nullptr, frameStride, static_cast<UINT>(framePixels.size()), framePixels.data()))) { continue; }

		for (UINT y = 0; y < frameHeight; ++y)
		{
			UINT dstY = baseOffsetY + frameTop + y;
			if (dstY >= canvasSize) continue;

			for (UINT x = 0; x < frameWidth; ++x)
			{
				UINT dstX = baseOffsetX + frameLeft + x;
				if (dstX >= canvasSize) continue;

				const BYTE* src = &framePixels[(y * frameStride) + (x * 4)];
				BYTE* dst = &canvas[(dstY * canvasStride) + (dstX * 4)];

				BYTE srcA = src[3];
				if (srcA == 255)
				{
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 255;
				}
				else if (srcA > 0)
				{
					float alpha = srcA / 255.0f;
					float invAlpha = 1.0f - alpha;
					dst[0] = static_cast<BYTE>(src[0] * alpha + dst[0] * invAlpha);
					dst[1] = static_cast<BYTE>(src[1] * alpha + dst[1] * invAlpha);
					dst[2] = static_cast<BYTE>(src[2] * alpha + dst[2] * invAlpha);
					dst[3] = static_cast<BYTE>(min(255.0f, srcA + dst[3] * invAlpha));
				}
			}
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = canvasSize;
		desc.Height = canvasSize;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA subData = {};
		subData.pSysMem = canvas.data();
		subData.SysMemPitch = canvasStride;

		ComPtr<ID3D11Texture2D> pTexture;
		if (SUCCEEDED(d3dDevice->CreateTexture2D(&desc, &subData, &pTexture)))
		{
			ID3D11ShaderResourceView* srv = nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;

			if (SUCCEEDED(d3dDevice->CreateShaderResourceView(pTexture.Get(), &srvDesc, &srv)))
			{
				outEmote.frames.push_back(srv);
			}
		}

		if (disposal == 2)
		{
			for (UINT y = 0; y < frameHeight; ++y)
			{
				UINT dstY = baseOffsetY + frameTop + y;
				if (dstY >= canvasSize) continue;

				for (UINT x = 0; x < frameWidth; ++x)
				{
					UINT dstX = baseOffsetX + frameLeft + x;
					if (dstX >= canvasSize) continue;

					BYTE* dst = &canvas[(dstY * canvasStride) + (dstX * 4)];
					dst[0] = 0;
					dst[1] = 0;
					dst[2] = 0;
					dst[3] = 0;
				}
			}
		}
		else if (disposal == 3 && !previousCanvas.empty())
		{
			canvas = previousCanvas;
		}
	}

	return !outEmote.frames.empty();
}