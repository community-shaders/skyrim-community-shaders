#pragma once

#include "Fonts.h"
#include "Menu.h"

namespace MenuFonts::Selector
{
	void RenderRolePickers(
		Menu& menu,
		Menu::ThemeSettings& themeSettings,
		const Util::Fonts::Catalog& catalog,
		Menu::FontRole role,
		size_t roleIndex);
}
