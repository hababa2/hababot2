#include "Theme.hpp"
#include "json.hpp"

#include <filesystem>
#include <fstream>
#include <charconv>

using json = nlohmann::json;

std::vector<ThemeData> Theme::themes;
size_t Theme::activeIndex = 0;

namespace
{
	ImVec4 ParseHexColor(std::string_view hex, const ImVec4& fallback)
	{
		if (hex.empty() || hex[0] != '#') { return fallback; }
		hex.remove_prefix(1);
		if (hex.size() != 6 && hex.size() != 8) { return fallback; }

		auto parseByte = [&](size_t offset) -> float {
			unsigned int val = 0;
			std::from_chars(hex.data() + offset, hex.data() + offset + 2, val, 16);
			return static_cast<float>(val) / 255.0f;
		};

		float r = parseByte(0);
		float g = parseByte(2);
		float b = parseByte(4);
		float a = (hex.size() == 8) ? parseByte(6) : 1.0f;
		return ImVec4(r, g, b, a);
	}

	std::string ToHexColor(const ImVec4& c)
	{
		auto byte = [](float v) { return static_cast<unsigned int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
		char buf[16];
		snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", byte(c.x), byte(c.y), byte(c.z), byte(c.w));
		return std::string(buf);
	}

	void ReadColor(const json& j, const char* key, ImVec4& dst)
	{
		if (j.contains(key) && j[key].is_string())
		{
			dst = ParseHexColor(j[key].get<std::string>(), dst);
		}
	}

	void ReadCard(const json& j, const char* key, CardColors& dst)
	{
		if (!j.contains(key) || !j[key].is_object()) { return; }
		const json& obj = j[key];
		ReadColor(obj, "header", dst.header);
		ReadColor(obj, "background", dst.background);
		ReadColor(obj, "accent", dst.accent);
	}
}

void Theme::Initialize(const std::string& activeThemeName)
{
	ReloadThemes();
	if (!SetActive(activeThemeName))
	{
		activeIndex = 0;
		ApplyToImGui();
	}
}

void Theme::ReloadThemes()
{
	std::string prevName = themes.empty() ? "" : themes[activeIndex].name;
	themes.clear();
	RegisterBuiltInThemes();
	LoadCustomThemesFromDisk();

	if (!prevName.empty() && SetActive(prevName)) { return; }
	activeIndex = 0;
	ApplyToImGui();
}

void Theme::RegisterBuiltInThemes()
{
	themes.push_back(ThemeData{});

	ThemeData slate{};
	slate.name = "Midnight Slate";
	slate.windowBg = { 0.07f, 0.08f, 0.10f, 0.85f };
	slate.popupBg = { 0.09f, 0.10f, 0.13f, 0.96f };
	slate.titleBar = { 0.16f, 0.20f, 0.27f, 1.00f };
	slate.titleBarText = { 0.92f, 0.95f, 1.00f, 1.00f };
	slate.accent = { 0.22f, 0.74f, 0.97f, 1.00f };
	slate.accentHovered = { 0.35f, 0.82f, 1.00f, 1.00f };
	slate.accentActive = { 0.14f, 0.60f, 0.85f, 1.00f };
	themes.push_back(std::move(slate));

	ThemeData crimson{};
	crimson.name = "Crimson Dark";
	crimson.windowBg = { 0.08f, 0.07f, 0.08f, 0.85f };
	crimson.popupBg = { 0.11f, 0.09f, 0.10f, 0.96f };
	crimson.titleBar = { 0.55f, 0.14f, 0.18f, 1.00f };
	crimson.titleBarText = { 0.98f, 0.94f, 0.94f, 1.00f };
	crimson.accent = { 0.82f, 0.22f, 0.28f, 1.00f };
	crimson.accentHovered = { 0.92f, 0.32f, 0.38f, 1.00f };
	crimson.accentActive = { 0.68f, 0.16f, 0.22f, 1.00f };
	themes.push_back(std::move(crimson));
}

void Theme::LoadCustomThemesFromDisk()
{
	std::error_code ec;
	if (!std::filesystem::exists("themes", ec)) { return; }

	for (const auto& entry : std::filesystem::directory_iterator("themes", ec))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".json") { continue; }

		std::ifstream in(entry.path());
		if (!in.is_open()) { continue; }

		json j = json::parse(in, nullptr, false);
		if (j.is_discarded() || !j.is_object()) { continue; }

		ThemeData t{};
		t.name = j.value("name", entry.path().stem().string());

		if (j.contains("ui") && j["ui"].is_object())
		{
			const json& ui = j["ui"];
			ReadColor(ui, "windowBg", t.windowBg);
			ReadColor(ui, "popupBg", t.popupBg);
			ReadColor(ui, "titleBar", t.titleBar);
			ReadColor(ui, "titleBarText", t.titleBarText);
			ReadColor(ui, "text", t.text);
			ReadColor(ui, "textMuted", t.textMuted);
			ReadColor(ui, "textDeleted", t.textDeleted);
			ReadColor(ui, "accent", t.accent);
			ReadColor(ui, "accentHovered", t.accentHovered);
			ReadColor(ui, "accentActive", t.accentActive);
			ReadColor(ui, "surface", t.surface);
			ReadColor(ui, "surfaceHovered", t.surfaceHovered);
			ReadColor(ui, "border", t.border);
			ReadColor(ui, "closeHovered", t.closeHovered);
			ReadColor(ui, "closeActive", t.closeActive);
			ReadColor(ui, "hypeTrainBar", t.hypeTrainBar);
		}

		if (j.contains("cards") && j["cards"].is_object())
		{
			const json& c = j["cards"];
			ReadCard(c, "mention", t.mention);
			ReadCard(c, "highlighted", t.highlighted);
			ReadCard(c, "firstChat", t.firstChat);
			ReadCard(c, "raider", t.raider);
			ReadCard(c, "returning", t.returning);
			ReadCard(c, "follow", t.follow);
			ReadCard(c, "sub", t.sub);
			ReadCard(c, "gift", t.gift);
			ReadCard(c, "bits", t.bits);
			ReadCard(c, "raid", t.raid);
			ReadCard(c, "redemption", t.redemption);
			ReadCard(c, "shoutout", t.shoutout);
			ReadCard(c, "hypeTrain", t.hypeTrain);
			ReadCard(c, "earlyBird1st", t.earlyBird1st);
			ReadCard(c, "earlyBird2nd", t.earlyBird2nd);
			ReadCard(c, "earlyBird3rd", t.earlyBird3rd);
		}

		auto it = std::find_if(themes.begin(), themes.end(),
			[&](const ThemeData& existing) { return existing.name == t.name; });
		if (it != themes.end()) { *it = std::move(t); }
		else { themes.push_back(std::move(t)); }
	}
}

bool Theme::SetActive(const std::string& themeName)
{
	for (size_t i = 0; i < themes.size(); ++i)
	{
		if (themes[i].name == themeName)
		{
			activeIndex = i;
			ApplyToImGui();
			return true;
		}
	}
	return false;
}

void Theme::ApplyToImGui()
{
	if (themes.empty() || ImGui::GetCurrentContext() == nullptr) { return; }

	const ThemeData& t = themes[activeIndex];
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	style.WindowRounding = 0.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 6.0f;
	style.GrabRounding = 4.0f;
	style.TabRounding = 4.0f;

	colors[ImGuiCol_Text] = t.text;
	colors[ImGuiCol_TextDisabled] = t.textMuted;
	colors[ImGuiCol_WindowBg] = t.windowBg;
	colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
	colors[ImGuiCol_PopupBg] = t.popupBg;
	colors[ImGuiCol_Border] = t.border;
	colors[ImGuiCol_TitleBg] = t.titleBar;
	colors[ImGuiCol_TitleBgActive] = t.titleBar;
	colors[ImGuiCol_TitleBgCollapsed] = t.titleBar;
	colors[ImGuiCol_FrameBg] = t.surface;
	colors[ImGuiCol_FrameBgHovered] = t.surfaceHovered;
	colors[ImGuiCol_FrameBgActive] = t.accentActive;
	colors[ImGuiCol_Button] = t.surface;
	colors[ImGuiCol_ButtonHovered] = t.accentHovered;
	colors[ImGuiCol_ButtonActive] = t.accentActive;
	colors[ImGuiCol_CheckMark] = t.accent;
	colors[ImGuiCol_SliderGrab] = t.accent;
	colors[ImGuiCol_SliderGrabActive] = t.accentHovered;
	colors[ImGuiCol_Header] = t.surface;
	colors[ImGuiCol_HeaderHovered] = t.accentHovered;
	colors[ImGuiCol_HeaderActive] = t.accentActive;
	colors[ImGuiCol_Separator] = t.border;
	colors[ImGuiCol_SeparatorHovered] = t.accentHovered;
	colors[ImGuiCol_SeparatorActive] = t.accent;
	colors[ImGuiCol_Tab] = t.surface;
	colors[ImGuiCol_TabHovered] = t.accentHovered;
	colors[ImGuiCol_TabSelected] = t.accent;
	colors[ImGuiCol_PlotHistogram] = t.hypeTrainBar;
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
	colors[ImGuiCol_ScrollbarGrab] = t.surface;
	colors[ImGuiCol_ScrollbarGrabHovered] = t.surfaceHovered;
	colors[ImGuiCol_ScrollbarGrabActive] = t.accent;
}

const ThemeData& Theme::Current()
{
	return themes[activeIndex];
}

const CardColors* Theme::GetCardColors(ChatCardType type)
{
	const ThemeData& t = Current();
	switch (type)
	{
	case ChatCardType::Mention:      return &t.mention;
	case ChatCardType::Highlighted:  return &t.highlighted;
	case ChatCardType::FirstChat:    return &t.firstChat;
	case ChatCardType::Raider:       return &t.raider;
	case ChatCardType::Returning:    return &t.returning;
	case ChatCardType::Follow:       return &t.follow;
	case ChatCardType::Sub:          return &t.sub;
	case ChatCardType::Gift:         return &t.gift;
	case ChatCardType::Bits:         return &t.bits;
	case ChatCardType::Raid:         return &t.raid;
	case ChatCardType::Redemption:   return &t.redemption;
	case ChatCardType::Shoutout:     return &t.shoutout;
	case ChatCardType::HypeTrain:    return &t.hypeTrain;
	case ChatCardType::EarlyBird1st: return &t.earlyBird1st;
	case ChatCardType::EarlyBird2nd: return &t.earlyBird2nd;
	case ChatCardType::EarlyBird3rd: return &t.earlyBird3rd;
	default:                         return nullptr;
	}
}

const std::vector<ThemeData>& Theme::GetAll()
{
	return themes;
}