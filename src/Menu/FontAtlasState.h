#pragma once

#include <array>
#include <string>

struct ImFont;

namespace MenuFonts
{
	/** Must match Menu::FontRole::Count; kept here so FontAtlasState.h need not include Menu.h. */
	inline constexpr size_t kFontRoleCount = 5;

	/**
	 * @brief Runtime font-atlas cache owned by MenuFonts (filled by ThemeManager::ReloadFont).
	 *
	 * Lives outside Menu.h so icon/font loading state is not piled onto the Menu singleton header.
	 * Role indices match Menu::FontRole.
	 */
	struct AtlasState
	{
		float cachedFontSize = 27.0f;
		mutable std::string cachedFontName = "Jost/Jost-Regular.ttf";
		std::array<std::string, kFontRoleCount> cachedFontFilesByRole = {
			"Jost/Jost-Regular.ttf",
			"Jost/Jost-Regular.ttf",
			"Jost/Jost-Regular.ttf",
			"Jost/Jost-Regular.ttf",
			"Jost/Jost-Regular.ttf",
		};
		mutable std::array<float, kFontRoleCount> cachedFontPixelSizesByRole = {};
		std::string cachedFontSignature;
		mutable std::array<ImFont*, kFontRoleCount> loadedFontRoles = {};
		/** Standalone icon typefaces, not MergeMode; PUA ranges overlap across families. */
		mutable ImFont* fontAwesomeIconFont = nullptr;
		mutable ImFont* tablerIconFont = nullptr;
		mutable ImFont* gameIconsFont = nullptr;

		bool pendingFontReload = false;
		bool wantsFontPreviewAtlas = false;

		[[nodiscard]] ImFont* GetFont(size_t roleIndex) const
		{
			return roleIndex < kFontRoleCount ? loadedFontRoles[roleIndex] : nullptr;
		}

		[[nodiscard]] ImFont* GetFontAwesomeIconFont() const { return fontAwesomeIconFont; }
		[[nodiscard]] ImFont* GetTablerIconFont() const { return tablerIconFont; }
		[[nodiscard]] ImFont* GetGameIconsFont() const { return gameIconsFont; }
	};

	[[nodiscard]] AtlasState& GetAtlasState();
}
