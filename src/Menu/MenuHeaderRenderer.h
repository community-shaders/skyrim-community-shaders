#pragma once

#include "Menu.h"

/**
 * @brief Renders the menu header bar with title, logo, and action icon buttons.
 *
 * Supports both docked and undocked window layouts, adapting the header
 * presentation accordingly. In docked mode the logo is drawn as a watermark
 * and action icons appear in the title bar; in undocked mode a full custom
 * header row is rendered.
 */
class MenuHeaderRenderer
{
public:
	/** @brief Describes a clickable action icon shown in the header bar. */
	struct ActionIcon
	{
		ID3D11ShaderResourceView* texture;  /**< @brief GPU texture for the icon image. */
		const char* tooltip;                /**< @brief Tooltip text shown on hover. */
		std::function<void()> callback;     /**< @brief Action invoked when the icon is clicked. */
	};

	/**
	 * @brief Renders the complete menu header area.
	 *
	 * Draws the title text, optional logo, and action icon buttons. The layout
	 * adapts based on whether the window is docked or floating.
	 *
	 * @param isDocked True if the menu window is docked into a tab bar.
	 * @param showLogo True if the Community Shaders logo should be displayed.
	 * @param canShowIcons True if action icons (save, load, clear cache, etc.) should be shown.
	 * @param uiScale Current UI scale factor for sizing icon buttons.
	 * @param uiIcons Reference to the loaded icon textures and sizes.
	 */
	static void RenderHeader(bool isDocked, bool showLogo, bool canShowIcons, float uiScale, const Menu::UIIcons& uiIcons);

private:
	static std::vector<ActionIcon> BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons);
	static void RenderActionIcons(const std::vector<ActionIcon>& actionIcons, bool isDocked, float uiScale);
	static void RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderWatermarkLogo(const Menu::UIIcons& uiIcons);
};