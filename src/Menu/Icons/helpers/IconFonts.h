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
#include "Utils/UI.h"

#include <format>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <unordered_map>

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
	 * Centres the glyph's ink, measured from the atlas once per glyph and size.
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

		// The quad comes from the font's glyph bbox, which can be looser than the ink (Tabler's all start at x=0)
		struct Ink
		{
			ImVec2 quad{ -1.0f, -1.0f };  // unmeasured; never matches a real quad
			ImVec4 box;  // ink bounds, in the same units as the quad
		};
		static std::unordered_map<ImGuiID, Ink> inkCache;
		const ImGuiID key = ImHashData(&c, sizeof(c), baked->BakedId);
		const ImVec2 quad(g->X1 - g->X0, g->Y1 - g->Y0);
		Ink& ink = inkCache[key];
		if (ink.quad.x != quad.x || ink.quad.y != quad.y) {
			ink = { quad, ImVec4(g->X0, g->Y0, g->X1, g->Y1) };
			ImFontAtlas* atlas = baked->OwnerFont ? baked->OwnerFont->OwnerAtlas : nullptr;
			ImTextureData* tex = atlas ? atlas->TexData : nullptr;
			const ImTextureRect* r = (tex && tex->Pixels) ? ImFontAtlasPackGetRectSafe(atlas, g->PackId) : nullptr;
			if (r && r->w > 0 && r->h > 0) {
				const int alpha = tex->Format == ImTextureFormat_RGBA32 ? 3 : 0;
				int x0 = r->w, y0 = r->h, x1 = -1, y1 = -1;
				for (int py = 0; py < r->h; ++py)
					for (int px = 0; px < r->w; ++px)
						if (static_cast<const unsigned char*>(tex->GetPixelsAt(r->x + px, r->y + py))[alpha]) {
							x0 = ImMin(x0, px), x1 = ImMax(x1, px);
							y0 = ImMin(y0, py), y1 = ImMax(y1, py);
						}
				// The atlas rect can be oversampled, so map pixels to the quad proportionally
				const float sx = quad.x / r->w, sy = quad.y / r->h;
				if (x1 >= 0)
					ink.box = ImVec4(g->X0 + x0 * sx, g->Y0 + y0 * sy, g->X0 + (x1 + 1) * sx, g->Y0 + (y1 + 1) * sy);
			}
		}

		const ImVec4& b = ink.box;
		const float scale = ImGui::GetFontSize() / baked->Size;
		const float x = boxMin.x + (boxSize.x - (b.z - b.x) * scale) * 0.5f - b.x * scale;
		const float y = boxMin.y + (boxSize.y - (b.w - b.y) * scale) * 0.5f - b.y * scale;
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

	/**
	 * @brief DrawCenteredGlyph at an explicit font size (pixels as rendered), regardless of the style and window
	 * font scales, so a glyph can be matched to a texture icon of a known size.
	 */
	inline void DrawCenteredGlyph(ImDrawList* drawList, ImVec2 boxMin, ImVec2 boxSize,
		float fontSize, GlyphRef glyph, ImU32 col)
	{
		ImFont* font = GetFont(glyph);
		if (!drawList || !font || !glyph)
			return;
		// PushFont takes a size before the style and window font scales; undo them
		const ImGuiContext& context = *ImGui::GetCurrentContext();
		const float fontScale = context.FontSizeBase > 0.0f ? context.FontSize / context.FontSizeBase : 1.0f;
		ImGui::PushFont(font, fontSize / ImMax(fontScale, 0.01f));
		drawList->AddText(GetCenteredGlyphPos(boxMin, boxSize, glyph.utf8), col, glyph.utf8);
		ImGui::PopFont();
	}

	/** @brief Icon-only text using the glyph's typeface. */
	inline void Text(GlyphRef glyph)
	{
		if (!glyph)
			return;
		FontGuard font(glyph);
		ImGui::TextUnformatted(glyph.utf8);
	}

	/**
	 * @brief Icon-only button (id is an ImGui ##suffix). The glyph is centred by its ink, not by its advance width
	 * as a button label would be, so glyphs with uneven side bearings (the three dots, say) sit in the middle.
	 */
	inline bool Button(const char* id, GlyphRef glyph, const ImVec2& size = ImVec2(0, 0))
	{
		if (!glyph)
			return false;
		ImVec2 buttonSize = size;
		if (buttonSize.x <= 0.0f || buttonSize.y <= 0.0f) {
			FontGuard font(glyph);
			// Square like the nav buttons: the glyph's line and the vertical padding on both axes
			const float side = ImGui::CalcTextSize(glyph.utf8).y + ImGui::GetStyle().FramePadding.y * 2.0f;
			if (buttonSize.x <= 0.0f)
				buttonSize.x = side;
			if (buttonSize.y <= 0.0f)
				buttonSize.y = side;
		}
		const bool clicked = Util::Button(id, buttonSize);
		DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectSize(), glyph, ImGui::GetColorU32(ImGuiCol_Text));
		return clicked;
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
		const ImVec2 iconSize = CalcGlyphSize(glyph);
		const ImVec2 labelSize = label ? ImGui::CalcTextSize(label) : ImVec2(0, 0);
		const float gap = LabeledButtonGap(glyph, label);

		ImVec2 btnSize = size;
		if (btnSize.x <= 0.0f)
			btnSize.x = MeasureLabeledButton(glyph, label);
		if (btnSize.y <= 0.0f)
			btnSize.y = ImGui::GetFrameHeight();

		ImGui::PushID(id);
		const bool clicked = Util::Button("##labeled", btnSize);
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImDrawList* dl = ImGui::GetWindowDrawList();

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
