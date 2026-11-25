#pragma once

#include "imgui.h"
#include <string>
#include <vector>

enum class ChatCardType
{
	None,
	Mention,
	Highlighted,
	FirstChat,
	Raider,
	Returning,
	Follow,
	Sub,
	Gift,
	Bits,
	Raid,
	Redemption,
	Shoutout,
	HypeTrain,
	EarlyBird1st,
	EarlyBird2nd,
	EarlyBird3rd
};

struct CardColors
{
	ImVec4 header = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 background = ImVec4(0.2f, 0.2f, 0.2f, 0.5f);
	ImVec4 accent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct ThemeData
{
	std::string name = "Wisteria";

	ImVec4 windowBg = ImVec4(0.10f, 0.10f, 0.12f, 0.80f);
	ImVec4 popupBg = ImVec4(0.12f, 0.12f, 0.15f, 0.96f);
	ImVec4 titleBar = ImVec4(0.79f, 0.63f, 0.86f, 1.00f);
	ImVec4 titleBarText = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	ImVec4 text = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
	ImVec4 textMuted = ImVec4(0.68f, 0.68f, 0.74f, 1.00f);
	ImVec4 textDeleted = ImVec4(0.45f, 0.45f, 0.48f, 1.00f);
	ImVec4 accent = ImVec4(0.57f, 0.27f, 0.97f, 1.00f);
	ImVec4 accentHovered = ImVec4(0.67f, 0.40f, 1.00f, 1.00f);
	ImVec4 accentActive = ImVec4(0.47f, 0.20f, 0.85f, 1.00f);
	ImVec4 surface = ImVec4(0.17f, 0.17f, 0.21f, 1.00f);
	ImVec4 surfaceHovered = ImVec4(0.23f, 0.23f, 0.29f, 1.00f);
	ImVec4 border = ImVec4(0.28f, 0.28f, 0.34f, 0.80f);
	ImVec4 closeHovered = ImVec4(0.85f, 0.20f, 0.20f, 0.90f);
	ImVec4 closeActive = ImVec4(0.70f, 0.15f, 0.15f, 1.00f);
	ImVec4 hypeTrainBar = ImVec4(0.56f, 0.27f, 0.97f, 1.00f);

	CardColors mention = { { 1.00f, 0.75f, 0.75f, 1.00f },{ 0.80f, 0.20f, 0.20f, 0.45f },{ 0.95f, 0.30f, 0.30f, 1.00f } };
	CardColors highlighted = { { 0.40f, 0.90f, 1.00f, 1.00f },{ 0.15f, 0.50f, 0.65f, 0.35f },{ 0.20f, 0.80f, 1.00f, 1.00f } };
	CardColors firstChat = { { 0.85f, 0.70f, 1.00f, 1.00f },{ 0.75f, 0.45f, 0.80f, 0.38f },{ 0.70f, 0.40f, 1.00f, 1.00f } };
	CardColors raider = { { 1.00f, 0.80f, 0.40f, 1.00f },{ 0.85f, 0.50f, 0.10f, 0.30f },{ 1.00f, 0.65f, 0.15f, 1.00f } };
	CardColors returning = { { 0.50f, 0.85f, 1.00f, 1.00f },{ 0.15f, 0.45f, 0.85f, 0.30f },{ 0.30f, 0.70f, 1.00f, 1.00f } };
	CardColors follow = { { 0.73f, 0.96f, 0.94f, 1.00f },{ 0.18f, 0.45f, 0.46f, 0.45f },{ 0.45f, 0.90f, 0.88f, 1.00f } };
	CardColors sub = { { 0.85f, 0.55f, 1.00f, 1.00f },{ 0.35f, 0.15f, 0.55f, 0.55f },{ 0.65f, 0.35f, 0.95f, 1.00f } };
	CardColors gift = { { 1.00f, 0.55f, 0.85f, 1.00f },{ 0.50f, 0.14f, 0.38f, 0.55f },{ 0.95f, 0.30f, 0.70f, 1.00f } };
	CardColors bits = { { 1.00f, 0.80f, 0.35f, 1.00f },{ 0.55f, 0.34f, 0.08f, 0.48f },{ 0.95f, 0.65f, 0.15f, 1.00f } };
	CardColors raid = { { 1.00f, 0.78f, 0.35f, 1.00f },{ 0.58f, 0.32f, 0.08f, 0.55f },{ 0.95f, 0.58f, 0.12f, 1.00f } };
	CardColors redemption = { { 0.45f, 0.95f, 0.75f, 1.00f },{ 0.10f, 0.42f, 0.30f, 0.50f },{ 0.20f, 0.82f, 0.58f, 1.00f } };
	CardColors shoutout = { { 1.00f, 0.70f, 0.45f, 1.00f },{ 0.55f, 0.24f, 0.08f, 0.50f },{ 0.95f, 0.48f, 0.18f, 1.00f } };
	CardColors hypeTrain = { { 0.85f, 0.65f, 1.00f, 1.00f },{ 0.36f, 0.16f, 0.65f, 0.55f },{ 0.65f, 0.35f, 1.00f, 1.00f } };
	CardColors earlyBird1st = { { 1.00f, 0.86f, 0.25f, 1.00f },{ 0.58f, 0.46f, 0.12f, 0.55f },{ 1.00f, 0.84f, 0.00f, 1.00f } };
	CardColors earlyBird2nd = { { 0.90f, 0.92f, 0.96f, 1.00f },{ 0.45f, 0.47f, 0.52f, 0.55f },{ 0.80f, 0.82f, 0.86f, 1.00f } };
	CardColors earlyBird3rd = { { 0.95f, 0.68f, 0.40f, 1.00f },{ 0.52f, 0.33f, 0.16f, 0.55f },{ 0.85f, 0.54f, 0.27f, 1.00f } };
};

class Theme
{
public:
	static void Initialize(const std::string& activeThemeName);
	static void ReloadThemes();
	static bool SetActive(const std::string& themeName);
	static void ApplyToImGui();

	static const ThemeData& Current();
	static const CardColors* GetCardColors(ChatCardType type);
	static const std::vector<ThemeData>& GetAll();

	static bool SaveThemeToFile(const ThemeData& theme, const std::string& path);

private:
	static std::vector<ThemeData> themes;
	static size_t activeIndex;

	static void RegisterBuiltInThemes();
	static void LoadCustomThemesFromDisk();
};