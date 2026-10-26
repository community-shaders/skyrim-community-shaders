#pragma once

#include "Menu/MenuShortcuts.h"

#include <imgui.h>
#include <vector>

/**
 * @brief The main menu's "Go to..." palette (Ctrl+K): a centred modal with a search field and a ranked list of
 * pages, features, settings and commands. The index and ranking live in GoToIndex.
 */
namespace GoToPalette
{
	/** @brief Asks for the palette to open on the next Draw(). Safe to call from a shortcut or a button. */
	void Open();

	/** @brief True while the palette is showing, or has been asked to open. */
	bool IsOpen();

	/**
	 * @brief Draws the palette while it is open and runs the entry the user picks.
	 * Call once per frame from inside the main menu window, outside any child window.
	 * @param shortcuts The menu's shortcut table, for command chords and the commands that run through it.
	 */
	void Draw(const std::vector<MenuShortcuts::Shortcut>& shortcuts);
}
