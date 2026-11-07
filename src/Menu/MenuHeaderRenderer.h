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
 * header row is rendered.
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

	/**
	 * @brief Renders the complete menu header area.
	 *
	 * Draws the title text, optional logo, and action icon buttons. The layout
	 * adapts based on whether the window is docked or floating.
	 *
	 * @param isDocked True if the menu window is docked into a tab bar.
	 * @param showLogo True if the Community Shaders logo should be displayed.
	 * @param uiScale Current UI scale factor for sizing icon buttons.
	 * @param uiIcons Reference to the loaded icon textures and sizes.
	 * @param shortcuts The menu's shortcut table, for tooltip hints.
	 */
	static void RenderHeader(bool isDocked, bool showLogo, float uiScale, const Util::IconLoader::UIIcons& uiIcons,
		const std::vector<MenuShortcuts::Shortcut>& shortcuts);

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
	 * @brief Draws one square nav button in the header (Home, issues).
	 * @param docked True in the docked title bar (foreground draw list, manual hit test).
	 * @param min Screen position of the button's top-left corner.
	 * @param size Edge of the square button.
	 * @param id Unique ImGui id for the undocked hit area.
	 * @param glyph The Tabler glyph.
	 * @param tooltip Hover tooltip.
	 * @param current Draws the button as the current page, in the accent colour.
	 * @param tintOverride Fixed icon colour (e.g. the error colour); null for the theme tint.
	 * @param onClick Invoked on click.
	 * @return True when clicked.
	 */
	static bool RenderNavButton(bool docked, ImVec2 min, float size, const char* id, Icons::GlyphRef glyph, const char* tooltip,
		bool current, const ImVec4* tintOverride, const std::function<void()>& onClick);
	/** @brief Edge of every header icon button: the action icons, Home, issues and Go to all share it. */
	static float GetHeaderIconSize(bool docked, float uiScale);
	/**
	 * @brief Draws the Home button and, while feature issues exist, the red issues button after it.
	 * @return The total width taken, including the gaps after each button.
	 */
	static float RenderNavButtons(bool docked, ImVec2 min, float size, float gap);
	static std::vector<ActionIcon> BuildActionIcons(const Util::IconLoader::UIIcons& uiIcons, const std::vector<MenuShortcuts::Shortcut>& shortcuts);
	/** @return The x of the leftmost icon's left edge, or the window's right edge when there are no icons. */
	static float RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	/**
	 * @brief Draws the centred "Search... Ctrl+K" pill, which opens the Go to palette.
	 * The pill starts at `left`, right after the Home and issues buttons, and shrinks to an icon-only button when the
	 * room up to `right` is tight; it is left out when even that does not fit.
	 * @param docked True in the docked title bar, where the pill is drawn on the foreground list like the icons.
	 * @param left Screen x the pill starts at (the right edge of the nav buttons).
	 * @param right Screen x the pill must stay left of.
	 * @param top Screen y of the row the pill is centred in.
	 * @param rowHeight Height of that row.
	 * @param shortcutHint The palette's chord, e.g. "Ctrl+K"; empty when unbound.
	 * @return The pill's right edge plus the gap after it, or `left` when nothing was drawn.
	 */
	static float RenderSearchPill(bool docked, float left, float right, float top, float rowHeight, float uiScale,
		const std::string& shortcutHint);
	static void RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderWatermarkLogo(const Util::IconLoader::UIIcons& uiIcons);
};
