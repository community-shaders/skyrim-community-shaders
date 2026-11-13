#pragma once

/**
 * @file SceneActionIcons.h
 * @brief Shared FA glyphs for Scene Manager list / toolbar actions.
 */

#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuIcons.h"

namespace SceneActionIcons
{
	inline constexpr Icons::GlyphRef kDelete = Icons::TI(ICON_TI_TRASH);
	inline constexpr Icons::GlyphRef kAdd = Icons::TI(ICON_TI_PLUS);
	inline constexpr Icons::GlyphRef kAdded = Icons::TI(ICON_TI_CHECK);
	inline constexpr Icons::GlyphRef kSave = Icons::TI(MenuIcons::kSave);
	inline constexpr Icons::GlyphRef kPause = Icons::TI(ICON_TI_PLAYER_PAUSE);
	inline constexpr Icons::GlyphRef kResume = Icons::TI(ICON_TI_PLAYER_PLAY);
	inline constexpr Icons::GlyphRef kCopy = Icons::TI(ICON_TI_COPY);
	/// Same share glyph the feature list and settings windows use for their export buttons.
	inline constexpr Icons::GlyphRef kExport = Icons::TI(ICON_TI_SHARE_2);
}
