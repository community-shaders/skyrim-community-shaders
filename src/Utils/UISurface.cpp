#include "Utils/UISurface.h"

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"

namespace Util::Surface
{
	using Constants = ThemeManager::Constants;

	namespace
	{
		constexpr size_t kRoleCount = static_cast<size_t>(Role::Count);

		/** @brief Clamps a float into [lo, hi]; non-finite values become the fallback. */
		void ClampField(float& value, float lo, float hi, float fallback)
		{
			if (!std::isfinite(value))
				value = fallback;
			value = std::clamp(value, lo, hi);
		}

		/** @brief Fill colour of a role: the window colour lifted by the role's fill lift, with an alpha multiplier. */
		ImU32 FillColor(const RoleSettings& settings, float alpha = 1.0f, float minFillAlpha = 0.0f)
		{
			const ImVec4 window = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
			const ImVec4 lifted = Anim::Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, window.w), settings.fillLift);
			return ImGui::GetColorU32(ImVec4(lifted.x, lifted.y, lifted.z, std::max(window.w * settings.fillAlpha, minFillAlpha) * alpha));
		}

		void ReadBool(const nlohmann::json& j, const char* key, bool& out)
		{
			if (const auto it = j.find(key); it != j.end() && it->is_boolean())
				out = it->get<bool>();
		}

		void ReadFloat(const nlohmann::json& j, const char* key, float& out)
		{
			if (const auto it = j.find(key); it != j.end() && it->is_number())
				out = it->get<float>();
		}
	}

	const char* RoleKey(Role role)
	{
		switch (role) {
		case Role::HomeCard:
			return "HomeCard";
		case Role::Toast:
			return "Toast";
		case Role::OverlayCard:
			return "OverlayCard";
		case Role::Palette:
			return "Palette";
		case Role::NavSelected:
			return "NavSelected";
		case Role::OptionCard:
			return "OptionCard";
		case Role::SectionCard:
			return "SectionCard";
		default:
			return "";
		}
	}

	RoleSettings Settings::Defaults(Role role)
	{
		RoleSettings s;
		s.fillLift = Constants::SURFACE_FILL_LIFT;
		s.borderAlpha = Constants::SURFACE_BORDER_ALPHA;
		s.shadowSpread = Constants::SURFACE_SHADOW_SPREAD;
		s.shadowAlpha = Constants::SURFACE_SHADOW_ALPHA;
		s.shadowOffsetX = Constants::SURFACE_SHADOW_OFFSET_X;
		s.shadowOffsetY = Constants::SURFACE_SHADOW_OFFSET_Y;
		s.padding = Constants::HOME_CARD_PADDING;
		s.accentBarWidth = Constants::ANIM_SELECTED_BAR_WIDTH;
		switch (role) {
		case Role::HomeCard:
			s.enabled = false;  // Home cards render as plain sections unless a theme turns the surface on
			break;
		case Role::Toast:
			// Small spread, offset down-right: reads as a backdrop shadow rather than a dark outline
			s.shadow = true;
			s.shadowSpread = 2.0f;
			s.shadowAlpha = 0.30f;
			s.shadowOffsetX = 3.0f;
			s.shadowOffsetY = 4.0f;
			break;
		case Role::OverlayCard:
		case Role::Palette:
			s.shadow = true;
			break;
		case Role::NavSelected:
			s.fillAlpha = Constants::NAV_SELECTED_FILL_ALPHA;
			break;
		default:
			break;
		}
		return s;
	}

	Settings::Settings()
	{
		for (size_t i = 0; i < kRoleCount; ++i)
			roles[i] = Defaults(static_cast<Role>(i));
	}

	void Settings::Sanitize()
	{
		for (size_t i = 0; i < kRoleCount; ++i) {
			auto& s = roles[i];
			const RoleSettings d = Defaults(static_cast<Role>(i));
			ClampField(s.fillLift, 0.0f, 0.5f, d.fillLift);
			ClampField(s.fillAlpha, 0.0f, 1.0f, d.fillAlpha);
			ClampField(s.rounding, -1.0f, 24.0f, -1.0f);
			if (s.rounding < 0.0f)
				s.rounding = -1.0f;
			ClampField(s.borderAlpha, 0.0f, 1.0f, d.borderAlpha);
			ClampField(s.borderColor.x, 0.0f, 1.0f, 1.0f);
			ClampField(s.borderColor.y, 0.0f, 1.0f, 1.0f);
			ClampField(s.borderColor.z, 0.0f, 1.0f, 1.0f);
			ClampField(s.borderColor.w, 0.0f, 1.0f, 0.2f);
			ClampField(s.shadowSpread, 0.0f, 32.0f, d.shadowSpread);
			ClampField(s.shadowAlpha, 0.0f, 1.0f, d.shadowAlpha);
			ClampField(s.shadowOffsetX, -Constants::SURFACE_SHADOW_OFFSET_MAX, Constants::SURFACE_SHADOW_OFFSET_MAX, d.shadowOffsetX);
			ClampField(s.shadowOffsetY, -Constants::SURFACE_SHADOW_OFFSET_MAX, Constants::SURFACE_SHADOW_OFFSET_MAX, d.shadowOffsetY);
			ClampField(s.padding, 0.0f, 48.0f, d.padding);
			ClampField(s.accentBarWidth, 0.0f, 12.0f, d.accentBarWidth);
		}
	}

	void to_json(nlohmann::json& j, const Settings& settings)
	{
		j = nlohmann::json::object();
		for (size_t i = 0; i < kRoleCount; ++i) {
			const auto& s = settings.roles[i];
			j[RoleKey(static_cast<Role>(i))] = {
				{ "Enabled", s.enabled },
				{ "FillLift", s.fillLift },
				{ "FillAlpha", s.fillAlpha },
				{ "Rounding", s.rounding },
				{ "Border", s.border },
				{ "BorderAlpha", s.borderAlpha },
				{ "UseBorderColor", s.useBorderColor },
				{ "BorderColor", { s.borderColor.x, s.borderColor.y, s.borderColor.z, s.borderColor.w } },
				{ "Shadow", s.shadow },
				{ "ShadowSpread", s.shadowSpread },
				{ "ShadowAlpha", s.shadowAlpha },
				{ "ShadowOffsetX", s.shadowOffsetX },
				{ "ShadowOffsetY", s.shadowOffsetY },
				{ "Padding", s.padding },
				{ "AccentBar", s.accentBar },
				{ "AccentBarWidth", s.accentBarWidth },
			};
		}
	}

	void from_json(const nlohmann::json& j, Settings& settings)
	{
		settings = Settings{};
		if (j.is_object()) {
			for (size_t i = 0; i < kRoleCount; ++i) {
				const auto it = j.find(RoleKey(static_cast<Role>(i)));
				if (it == j.end() || !it->is_object())
					continue;
				auto& s = settings.roles[i];
				ReadBool(*it, "Enabled", s.enabled);
				ReadFloat(*it, "FillLift", s.fillLift);
				ReadFloat(*it, "FillAlpha", s.fillAlpha);
				ReadFloat(*it, "Rounding", s.rounding);
				ReadBool(*it, "Border", s.border);
				ReadFloat(*it, "BorderAlpha", s.borderAlpha);
				ReadBool(*it, "UseBorderColor", s.useBorderColor);
				if (const auto c = it->find("BorderColor"); c != it->end() && c->is_array() && c->size() >= 4) {
					float rgba[4] = { s.borderColor.x, s.borderColor.y, s.borderColor.z, s.borderColor.w };
					for (size_t k = 0; k < 4; ++k)
						if ((*c)[k].is_number())
							rgba[k] = (*c)[k].get<float>();
					s.borderColor = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
				}
				ReadBool(*it, "Shadow", s.shadow);
				ReadFloat(*it, "ShadowSpread", s.shadowSpread);
				ReadFloat(*it, "ShadowAlpha", s.shadowAlpha);
				ReadFloat(*it, "ShadowOffsetX", s.shadowOffsetX);
				ReadFloat(*it, "ShadowOffsetY", s.shadowOffsetY);
				ReadFloat(*it, "Padding", s.padding);
				ReadBool(*it, "AccentBar", s.accentBar);
				ReadFloat(*it, "AccentBarWidth", s.accentBarWidth);
			}
		}
		settings.Sanitize();
	}

	const RoleSettings& Get(Role role)
	{
		static const Settings defaults;
		const size_t index = static_cast<size_t>(role);
		if (index >= kRoleCount)
			return defaults.roles[0];
		if (globals::menu)
			return globals::menu->GetTheme().Surfaces.roles[index];
		return defaults.roles[index];
	}

	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, Role role, const Options& options)
	{
		const RoleSettings& s = Get(role);
		if (!s.enabled)
			return;

		const float scale = GetUIScale();
		const float styleRounding = options.rounding >= 0.0f ? options.rounding : ImGui::GetStyle().ChildRounding;
		const float rounding = s.rounding >= 0.0f ? s.rounding * scale : styleRounding;

		// A bounded surface (a card in a scrolling page) never draws past its bounds
		const bool bounded = options.clip.z > options.clip.x && options.clip.w > options.clip.y;
		if (s.shadow && s.shadowAlpha > 0.0f) {
			// The shadow spills outside the window that owns the list, so lift its clip rect for it
			const float grow = s.shadowSpread * scale;
			const float shiftX = s.shadowOffsetX * scale;
			const float dropY = s.shadowOffsetY * scale;
			if (bounded)
				drawList->PushClipRect(ImVec2(options.clip.x, options.clip.y), ImVec2(options.clip.z, options.clip.w), false);
			else
				drawList->PushClipRectFullScreen();
			drawList->AddRectFilled(ImVec2(min.x - grow + shiftX, min.y - grow + dropY), ImVec2(max.x + grow + shiftX, max.y + grow + dropY),
				ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, s.shadowAlpha * options.alpha)), rounding + grow);
			drawList->PopClipRect();
		}

		// A window's inner clip rect trims its left and right edges, so the surface takes its own rect as the clip
		if (bounded)
			drawList->PushClipRect(ImVec2(std::max(min.x, options.clip.x), std::max(min.y, options.clip.y)),
				ImVec2(std::min(max.x, options.clip.z), std::min(max.y, options.clip.w)), false);
		else
			drawList->PushClipRect(min, max, false);
		drawList->AddRectFilled(min, max, FillColor(s, options.alpha, options.minFillAlpha), rounding);
		if (s.border) {
			ImU32 color;
			if (options.borderColor)
				color = options.borderColor;
			else if (s.useBorderColor)
				color = ImGui::GetColorU32(ImVec4(s.borderColor.x, s.borderColor.y, s.borderColor.z, s.borderColor.w * options.alpha));
			else
				color = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, s.borderAlpha * options.alpha));
			drawList->AddRect(min, max, color, rounding);
		}
		drawList->PopClipRect();
	}

	ImVec4 ParentClipRect()
	{
		const ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiWindow* parent = window ? window->ParentWindow : nullptr;
		if (!parent)
			return {};
		return { parent->ClipRect.Min.x, parent->ClipRect.Min.y, parent->ClipRect.Max.x, parent->ClipRect.Max.y };
	}

	void DrawAccentFrame(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color, float rounding, float borderThickness,
		float stripeWidth)
	{
		drawList->PushClipRect(min, max, false);
		// A stroke is centred on its path, so the path is inset by half the thickness to keep the whole stroke inside
		const float half = borderThickness * 0.5f;
		drawList->AddRect(ImVec2(min.x + half, min.y + half), ImVec2(max.x - half, max.y - half), color, std::max(0.0f, rounding - half), 0,
			borderThickness);
		if (stripeWidth > 0.0f) {
			drawList->PushClipRect(min, ImVec2(min.x + stripeWidth, max.y), true);
			drawList->AddRectFilled(min, max, color, rounding);
			drawList->PopClipRect();
		}
		drawList->PopClipRect();
	}
}
