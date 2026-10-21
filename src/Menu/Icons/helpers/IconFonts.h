#pragma once

/**
 * @file IconFonts.h
 * @brief Standalone FA / Tabler / Game Icons fonts + family-tagged glyph helpers.
 *
 * Icon typefaces are loaded as separate ImFonts (not MergeMode) so overlapping
 * PUA codepoints never steal each other's glyphs. Pass Icons::FA / TI / GI
 * wrappers (or GlyphRef) into the draw helpers so the right face is pushed.
 *
 * Use Tabler for UI. FA and Game Icons are for game-world icons (places, creatures, lore).
 */

#include "Menu/FontAtlasState.h"

#include <format>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>

namespace Icons
{
	enum class Family : uint8_t
	{
		FontAwesome,
		Tabler,
		GameIcons
	};

	/** @brief UTF-8 icon glyph tagged with its typeface. */
	struct GlyphRef
	{
		Family family = Family::FontAwesome;
		const char* utf8 = nullptr;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return utf8 && utf8[0] != '\0'; }
		[[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }

		[[nodiscard]] friend constexpr bool operator==(GlyphRef a, GlyphRef b) noexcept
		{
			return a.family == b.family && a.utf8 == b.utf8;
		}
		[[nodiscard]] friend constexpr bool operator!=(GlyphRef a, GlyphRef b) noexcept { return !(a == b); }
	};

	[[nodiscard]] constexpr GlyphRef FA(const char* utf8) noexcept { return { Family::FontAwesome, utf8 }; }
	[[nodiscard]] constexpr GlyphRef TI(const char* utf8) noexcept { return { Family::Tabler, utf8 }; }
	[[nodiscard]] constexpr GlyphRef GI(const char* utf8) noexcept { return { Family::GameIcons, utf8 }; }

	[[nodiscard]] inline ImFont* GetFont(Family family)
	{
		const auto& atlas = MenuFonts::GetAtlasState();
		switch (family) {
		case Family::FontAwesome:
			return atlas.GetFontAwesomeIconFont();
		case Family::Tabler:
			return atlas.GetTablerIconFont();
		case Family::GameIcons:
			return atlas.GetGameIconsFont();
		}
		return nullptr;
	}

	[[nodiscard]] inline ImFont* GetFont(GlyphRef glyph) { return GetFont(glyph.family); }

	/** @brief Push the glyph's standalone icon font for the enclosing scope, optionally scaled from its loaded size. */
	class FontGuard
	{
	public:
		explicit FontGuard(Family family, float scale = 1.0f)
		{
			if (ImFont* font = GetFont(family)) {
				ImGui::PushFont(font, font->LegacySize * scale);
				pushed_ = true;
			}
		}
		explicit FontGuard(GlyphRef glyph, float scale = 1.0f) :
			FontGuard(glyph.family, scale)
		{}
		~FontGuard()
		{
			if (pushed_)
				ImGui::PopFont();
		}

		FontGuard(const FontGuard&) = delete;
		FontGuard& operator=(const FontGuard&) = delete;

	private:
		bool pushed_ = false;
	};

	[[nodiscard]] inline ImVec2 CalcGlyphSize(GlyphRef glyph)
	{
		if (!glyph)
			return ImVec2(0.0f, 0.0f);
		FontGuard font(glyph);
		return ImGui::CalcTextSize(glyph.utf8);
	}

	/**
	 * @brief Top-left pos for AddText that centres the glyph in boxMin+boxSize (current font), pixel-snapped.
	 * Centres the glyph quad; on an axis where the quad starts at the origin (some icon fonts store bounds anchored
	 * there), the advance / line box is centred instead.
	 */
	[[nodiscard]] inline ImVec2 GetCenteredGlyphPos(ImVec2 boxMin, ImVec2 boxSize, const char* utf8Glyph)
	{
		unsigned int c = 0;
		if (utf8Glyph && utf8Glyph[0] != '\0')
			ImTextCharFromUtf8(&c, utf8Glyph, nullptr);
		ImFontBaked* baked = ImGui::GetFontBaked();
		if (c == 0 || !baked)
			return boxMin;
		const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(c));  // never null: falls back to the fallback glyph
		const float fontSize = ImGui::GetFontSize();
		const float scale = fontSize / baked->Size;
		// Half-pixel tolerance covers sub-pixel raster offsets.
		const bool anchoredX = ImFabs(g->X0) < 0.5f;
		const bool anchoredY = ImFabs(g->Y1 - IM_ROUND(baked->Ascent)) < 0.5f;  // quad bottom on the baseline
		const float x = anchoredX ? boxMin.x + (boxSize.x - g->AdvanceX * scale) * 0.5f :
		                            boxMin.x + (boxSize.x - (g->X1 - g->X0) * scale) * 0.5f - g->X0 * scale;
		const float y = anchoredY ? boxMin.y + (boxSize.y - fontSize) * 0.5f :
		                            boxMin.y + (boxSize.y - (g->Y1 - g->Y0) * scale) * 0.5f - g->Y0 * scale;
		// ImFont::RenderText truncates the position; round here so the glyph isn't biased up-left.
		return ImVec2(ImFloor(x + 0.5f), ImFloor(y + 0.5f));
	}

	inline void DrawCenteredGlyph(ImDrawList* drawList, ImVec2 boxMin, ImVec2 boxSize,
		GlyphRef glyph, ImU32 col)
	{
		if (!drawList || !glyph)
			return;
		FontGuard font(glyph);
		drawList->AddText(GetCenteredGlyphPos(boxMin, boxSize, glyph.utf8), col, glyph.utf8);
	}

	/** @brief Icon-only text using the glyph's typeface. */
	inline void Text(GlyphRef glyph)
	{
		if (!glyph)
			return;
		FontGuard font(glyph);
		ImGui::TextUnformatted(glyph.utf8);
	}

	/** @brief Icon-only button (id is an ImGui ##suffix; visible label is the glyph). */
	inline bool Button(const char* id, GlyphRef glyph, const ImVec2& size = ImVec2(0, 0))
	{
		if (!glyph)
			return false;
		FontGuard font(glyph);
		return ImGui::Button(std::format("{}{}", glyph.utf8, id).c_str(), size);
	}

	/** @brief Gap LabeledButton leaves between its glyph and label. */
	inline float LabeledButtonGap(GlyphRef glyph, const char* label)
	{
		return (glyph && label && label[0]) ? ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;
	}

	/** @brief Width LabeledButton takes at its natural size, for right-aligning it. */
	inline float MeasureLabeledButton(GlyphRef glyph, const char* label)
	{
		const float labelWidth = label ? ImGui::CalcTextSize(label).x : 0.0f;
		return CalcGlyphSize(glyph).x + LabeledButtonGap(glyph, label) + labelWidth + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	/**
	 * @brief Button with a leading icon glyph and body-font label.
	 * Icon fonts have no Latin glyphs, so the two faces are drawn separately.
	 */
	inline bool LabeledButton(const char* id, GlyphRef glyph, const char* label,
		const ImVec2& size = ImVec2(0, 0))
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 iconSize = CalcGlyphSize(glyph);
		const ImVec2 labelSize = label ? ImGui::CalcTextSize(label) : ImVec2(0, 0);
		const float gap = LabeledButtonGap(glyph, label);

		ImVec2 btnSize = size;
		if (btnSize.x <= 0.0f)
			btnSize.x = MeasureLabeledButton(glyph, label);
		if (btnSize.y <= 0.0f)
			btnSize.y = ImGui::GetFrameHeight();

		ImGui::PushID(id);
		const bool clicked = ImGui::InvisibleButton("##labeled", btnSize);
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive()  ? ImGuiCol_ButtonActive :
											ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered :
																	 ImGuiCol_Button);
		dl->AddRectFilled(min, max, bg, style.FrameRounding);

		const float contentW = iconSize.x + gap + labelSize.x;
		float x = min.x + (btnSize.x - contentW) * 0.5f;
		const float midY = (min.y + max.y) * 0.5f;
		const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);

		if (glyph) {
			DrawCenteredGlyph(dl, ImVec2(x, min.y), ImVec2(iconSize.x, btnSize.y), glyph, col);
			x += iconSize.x + gap;
		}
		if (label && label[0])
			dl->AddText(ImVec2(x, midY - labelSize.y * 0.5f), col, label);

		ImGui::PopID();
		return clicked;
	}
}
