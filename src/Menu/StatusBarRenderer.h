#pragma once

#include "Menu/MenuShortcuts.h"

#include <vector>

/**
 * @brief The thin status bar at the bottom of the main menu window (Theme.ShowFooter).
 *
 * The left end holds the Settings and Profiling page buttons. Next to them the left side shows what needs attention, each part only while it applies: unsaved changes,
 * shader compile progress, failed shaders and feature issues. The right side shows the GPU, the
 * game runtime and version, and the D3D12 swap chain state, ellipsized when the left side needs
 * the room.
 */
namespace StatusBarRenderer
{
	/** @brief Height to reserve below the content for the bar, including its separator. */
	float GetHeight();

	/**
	 * @brief Draws the bar at the cursor; call after the content, inside the main menu window.
	 * @param shortcuts The menu's shortcut table, for the Save hint.
	 * @param showStatus False draws only the Settings and Profiling buttons (Show Status Bar is off).
	 */
	void Render(const std::vector<MenuShortcuts::Shortcut>& shortcuts, bool showStatus = true);
}
