#pragma once

class Menu;

/** @brief Utilities for loading and rendering custom mouse cursor images. */
namespace Util::CursorLoader
{
	/**
	 * @brief Migrates legacy single-cursor settings to the per-cursor-type format.
	 *
	 * Copies the old global cursor file and hotspot into the Arrow slot if the
	 * Arrow slot is empty and legacy settings exist.
	 *
	 * @param theme Theme settings to migrate in place.
	 */
	void MigrateLegacyCursorSettings(Menu::ThemeSettings& theme);

	/**
	 * @brief Reloads all custom cursor textures from disk.
	 *
	 * Releases any previously loaded cursors, then loads cursor images for each
	 * ImGui cursor type from the active theme directory or the shared cursors
	 * directory. Does nothing if custom cursors are disabled in the theme.
	 *
	 * @param menu Pointer to the Menu instance providing theme and path context.
	 * @return true if the reload completed (even if no images were found), false
	 *         if the D3D device was unavailable or menu was null.
	 */
	bool Reload(Menu* menu);

	/** @brief Returns the number of cursor image slots that have a loaded texture. */
	int GetLoadedCount();

	/** @brief Releases all loaded cursor textures and resets cursor state. */
	void Shutdown();

	/**
	 * @brief Draws the custom cursor image at the current mouse position.
	 *
	 * Renders the appropriate cursor image for the active ImGui cursor type,
	 * applying the configured scale and hotspot offset. Falls back to the
	 * default ImGui cursor if no custom texture is loaded for the active type.
	 *
	 * @param menu Reference to the Menu instance for reading theme settings.
	 */
	void DrawCustomCursor(const Menu& menu);
}
