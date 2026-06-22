#pragma once

struct ID3D11Device;
class Menu;

namespace Util
{
	/** @brief Loads menu icon textures from disk into GPU shader resource views. */
	namespace IconLoader
	{
		/**
		 * @brief Loads all menu icon textures including theme-specific overrides.
		 *
		 * Releases any previously loaded icon textures, then loads the full set of
		 * action icons, category icons, and the logo from the base icon directory.
		 * Falls back from monochrome to colored variants when a monochrome icon is
		 * missing. After loading base icons, applies any theme-specific overrides
		 * from the active theme directory.
		 *
		 * @param menu Pointer to the Menu instance whose UIIcons will be populated.
		 * @return true if at least one icon was loaded successfully, false otherwise.
		 */
		bool InitializeMenuIcons(Menu* menu);
	}
}
