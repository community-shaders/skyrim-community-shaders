#pragma once

#include "Menu/IconLoader.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuShortcuts.h"

#include <functional>
#include <string>
#include <vector>

/**
 * @brief Renders the menu header bar with title, logo, and action icon buttons.
 *
 * Supports both docked and undocked window layouts, adapting the header
 * presentation accordingly. In docked mode the logo is drawn as a watermark
 * and action icons appear in the title bar; in undocked mode a full custom
 * header row is rendered. A help icon after the actions opens a popover with
 * the keyboard shortcuts, the FAQ, links and build information.
 */
class MenuHeaderRenderer
{
public:
	/** @brief Describes a clickable action icon shown in the header bar. */
	struct ActionIcon
	{
		ID3D11ShaderResourceView* texture; /**< @brief GPU texture for the icon image; null draws the glyph instead. */
		Icons::GlyphRef glyph;             /**< @brief Font glyph drawn when the texture is missing. */
		std::string tooltip;               /**< @brief Tooltip text shown on hover. */
		std::function<void()> callback;    /**< @brief Action invoked when the icon is clicked. */
		bool unsaved = false;              /**< @brief Tints the icon as a save with unsaved changes. */
	};

	/** @brief Help popover and FAQ modal state, owned by Menu and passed in each frame. */
	struct HelpState
	{
		bool openPopover = false; /**< @brief Request: open the help popover this frame (the ? icon, F1). */
		bool openFaq = false;     /**< @brief Request: open the FAQ modal this frame. */
		bool faqOpen = false;     /**< @brief The FAQ modal is showing; its close button clears it. */
	};

	/**
	 * @brief Renders the complete menu header area.
	 *
	 * Draws the title text, optional logo, and action icon buttons. The layout
	 * adapts based on whether the window is docked or floating. Also draws the
	 * help popover and FAQ modal while they are open.
	 *
	 * @param isDocked True if the menu window is docked into a tab bar.
	 * @param showLogo True if the Community Shaders logo should be displayed.
	 * @param uiScale Current UI scale factor for sizing icon buttons.
	 * @param uiIcons Reference to the loaded icon textures and sizes.
	 * @param shortcuts The menu's shortcut table, for tooltip hints and the help list.
	 * @param help Help popover and FAQ modal state; its requests are cleared once handled.
	 */
	static void RenderHeader(bool isDocked, bool showLogo, float uiScale, const Util::IconLoader::UIIcons& uiIcons,
		const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help);

	/** @brief "Community Shaders <version>", as the header title shows it. */
	static std::string GetTitleText();

	/** @brief The build label shown after the title on development builds; empty on release builds. */
	static std::string GetBuildTagText();

	/** @brief "Game Version: <runtime> <version>". */
	static std::string GetGameVersionText();

	/** @brief "D3D12 Swap Chain: Active|Inactive". */
	static std::string GetSwapChainText();

	/** @brief "GPU: <adapter name>". */
	static std::string GetGpuText();

private:
	static std::vector<ActionIcon> BuildActionIcons(const Util::IconLoader::UIIcons& uiIcons,
		const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help);
	static void RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderWatermarkLogo(const Util::IconLoader::UIIcons& uiIcons);
	static void RenderHelpPopover(const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help);
	static void RenderFaqModal(HelpState& help);
};
