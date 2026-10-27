#pragma once

#include "Menu/MenuShortcuts.h"

#include <vector>

/**
 * @brief The thin status bar at the bottom of the main menu window (Theme.ShowFooter).
 *
 * The left end holds the Settings and Profiling page buttons (with the bar turned off they sit at the bottom of the left
 * pane instead, see RenderPageButtons). Next to them the left side shows what needs attention, each part only while it applies: unsaved changes,
 * shader compile progress, failed shaders and feature issues. The right side shows the GPU, the
 * game runtime and version, and the D3D12 swap chain state, ellipsized when the left side needs
 * the room.
 */
namespace StatusBarRenderer
{
	/** @brief Height to reserve below the content for the bar, including its separator. */
	float GetHeight();

	/**
	 * @brief Draws the bar at the cursor; call after the content, inside the main menu window, and only while
	 * Theme.ShowFooter is on.
	 * @param shortcuts The menu's shortcut table, for the Save hint.
	 */
	void Render(const std::vector<MenuShortcuts::Shortcut>& shortcuts);

	/** @brief Height the left pane reserves at its bottom for RenderPageButtons. */
	float GetPageButtonsHeight();

	/**
	 * @brief Draws the Settings and Profiling buttons at the cursor, under a separator: the left pane's bottom row
	 * while the status bar is turned off, so both pages stay reachable.
	 */
	void RenderPageButtons();
}
