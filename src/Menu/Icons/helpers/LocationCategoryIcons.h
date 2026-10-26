#pragma once

/**
 * @file LocationCategoryIcons.h
 * @brief Font Awesome glyphs for CS Editor location-target categories
 *        (worldspace / LocType / region / location / cell).
 */

#include "IconsFontAwesome5.h"
#include "Menu/Icons/helpers/IconFonts.h"

#include <cstdint>

namespace LocationCategoryIcons
{
	/** @brief Same order as SceneSettingsManager::LocationTargetType. */
	enum class Kind : uint8_t
	{
		Worldspace,
		LocationType,
		Region,
		Location,
		Cell
	};

	inline constexpr Icons::GlyphRef kWorldspace = Icons::FA(ICON_FA_GLOBE_EUROPE);
	inline constexpr Icons::GlyphRef kLocationType = Icons::FA(ICON_FA_TAG);
	inline constexpr Icons::GlyphRef kRegion = Icons::FA(ICON_FA_MAP);
	inline constexpr Icons::GlyphRef kCell = Icons::FA(ICON_FA_BORDER_ALL);
	inline constexpr Icons::GlyphRef kLocation = Icons::FA(ICON_FA_MAP_MARKER_ALT);

	/** @brief Glyph for a location-target category. */
	[[nodiscard]] inline Icons::GlyphRef Resolve(Kind kind) noexcept
	{
		switch (kind) {
		case Kind::Worldspace:
			return kWorldspace;
		case Kind::LocationType:
			return kLocationType;
		case Kind::Region:
			return kRegion;
		case Kind::Cell:
			return kCell;
		case Kind::Location:
		default:
			return kLocation;
		}
	}
}
