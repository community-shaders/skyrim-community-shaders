#pragma once

#include "Menu/MenuShortcuts.h"

#include <imgui.h>
#include <vector>

/**
 * @brief The main menu's "Go to..." palette (Ctrl+K): a centred modal with a search field, a row of scopes (All,
 * Pages, Features, Settings, Commands) and a ranked list of matches. The field also holds the sort and filter
 * button of the feature list. The index and ranking live in GoToIndex.
 */
namespace GoToPalette
{
	/** @brief What the palette lists. Tab and Shift+Tab cycle through these in order. */
	enum class Scope : std::uint8_t
	{
		All,
		Pages,
		Features,
		Settings,
		Commands
	};

	/**
	 * @brief Asks for the palette to open on the next Draw(). Safe to call from a shortcut or a button.
	 * @param scope The scope it opens on; Ctrl+F opens on Features.
	 */
	void Open(Scope scope = Scope::All);

	/** @brief True while the palette is showing, or has been asked to open. */
	bool IsOpen();

	/**
	 * @brief Draws the palette while it is open and runs the entry the user picks.
	 * Call once per frame from inside the main menu window, outside any child window.
	 * @param shortcuts The menu's shortcut table, for command chords and the commands that run through it.
	 */
	void Draw(const std::vector<MenuShortcuts::Shortcut>& shortcuts);
}
