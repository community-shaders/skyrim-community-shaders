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
		Icons::GlyphRef glyph;          /**< @brief Tabler glyph drawn for the icon. */
		std::string tooltip;            /**< @brief Tooltip text shown on hover. */
		std::function<void()> callback; /**< @brief Action invoked when the icon is clicked. */
		bool unsaved = false;           /**< @brief Tints the icon as a save with unsaved changes. */
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

	/**
	 * @brief A transparent glyph button in the header's icon style, for use outside the header (Home cards).
	 * @param id ImGui id, e.g. "##HomeSave".
	 * @param glyph The Tabler glyph, from MenuIcons.
	 * @param glyphBox Edge of the square the glyph is drawn in; the button adds frame padding around it.
	 * @param tooltip Hover tooltip; null for none.
	 * @param active Draws the button as the current selection, in the accent colour.
	 * @return True when clicked.
	 */
	static bool GlyphIconButton(const char* id, Icons::GlyphRef glyph, float glyphBox, const char* tooltip, bool active = false);

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
	/**
	 * @brief Draws the Home button, which opens the Home page and highlights while that page is showing.
	 * @param docked True in the docked title bar (foreground draw list, manual hit test).
	 * @param min Screen position of the button's top-left corner.
	 * @param size Edge of the square button.
	 * @return True when clicked.
	 */
	static bool RenderHomeButton(bool docked, ImVec2 min, float size);
	static std::vector<ActionIcon> BuildActionIcons(const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help);
	/** @return The x of the leftmost icon's left edge, or the window's right edge when there are no icons. */
	static float RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	/**
	 * @brief Draws the centred "Search... Ctrl+K" pill, which opens the Go to palette.
	 * The pill sits at the centre of centerX when it fits between left and right, is nudged aside when the title
	 * or icons are in the way, shrinks to an icon-only button when the room is tight and is left out when even that does not fit.
	 * @param docked True in the docked title bar, where the pill is drawn on the foreground list like the icons.
	 * @param left Right edge of the title, in screen space.
	 * @param right Left edge of the icons, in screen space.
	 * @param centerX Screen x the pill centres on.
	 * @param top Screen y of the row the pill is centred in.
	 * @param rowHeight Height of that row.
	 * @param shortcutHint The palette's chord, e.g. "Ctrl+K"; empty when unbound.
	 */
	static void RenderSearchPill(bool docked, float left, float right, float centerX, float top, float rowHeight, float uiScale,
		const std::string& shortcutHint);
	static void RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderWatermarkLogo(const Util::IconLoader::UIIcons& uiIcons);
	static void RenderHelpPopover(const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help);
	static void RenderFaqModal(HelpState& help);
};
