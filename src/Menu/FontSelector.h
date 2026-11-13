#pragma once

#include "Fonts.h"
#include "Menu.h"

namespace MenuFonts::Selector
{
	/** @brief A font role's family and style pickers and its scale slider (plus the feature header scale for Title). */
	void RenderRolePickers(
		Menu& menu,
		Menu::ThemeSettings& themeSettings,
		const Util::Fonts::Catalog& catalog,
		Menu::FontRole role,
		size_t roleIndex);
}
