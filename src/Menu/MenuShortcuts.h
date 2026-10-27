#pragma once

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief The main menu's keyboard shortcuts.
 *
 * One table drives the dispatch, the shortcut hints in tooltips and the list in the header's help
 * popover, so the three never disagree. Menu builds the table once, with the actions bound to its
 * own state, and dispatches it once per frame from inside its window.
 */
namespace MenuShortcuts
{
	/** @brief Stable shortcut IDs. Entries that run the same action (Redo's two chords) share one. */
	namespace Ids
	{
		inline constexpr std::string_view kSave = "menu.save";
		inline constexpr std::string_view kUndo = "menu.undo";
		inline constexpr std::string_view kRedo = "menu.redo";
		inline constexpr std::string_view kGoTo = "menu.go_to";
		inline constexpr std::string_view kFocusSearch = "menu.focus_search";
		inline constexpr std::string_view kPreviousFeature = "menu.previous_feature";
		inline constexpr std::string_view kNextFeature = "menu.next_feature";
		inline constexpr std::string_view kTogglePin = "menu.toggle_pin";
		inline constexpr std::string_view kHome = "menu.page_home";
		inline constexpr std::string_view kPresets = "menu.page_presets";
		inline constexpr std::string_view kGeneral = "menu.page_general";
		inline constexpr std::string_view kOpenEditor = "menu.open_editor";
		inline constexpr std::string_view kHelp = "menu.help";
	}

	/** @brief One keyboard shortcut. */
	struct Shortcut
	{
		std::string_view id;            ///< Stable ID from Ids
		ImGuiKeyChord chord;            ///< Key plus modifiers, e.g. ImGuiMod_Ctrl | ImGuiKey_S
		const char* (*label)();         ///< The translated label: wraps T("menu.shortcuts.<action>", "...") so the extractor sees key and text
		std::function<bool()> enabled;  ///< Whether the action can run now; empty means always
		std::function<void()> run;      ///< Runs the action
		bool repeat = false;            ///< Fires again while the chord is held
	};

	/** @brief What the shortcuts do, bound by Menu to its own state. Empty actions are left out of the table. */
	struct Actions
	{
		std::function<void()> save;
		std::function<void()> undo;
		std::function<void()> redo;
		std::function<void()> openGoTo;        ///< Opens the "Go to..." palette
		std::function<void()> focusSearch;     ///< Ctrl+F: opens the Go to palette on Features
		std::function<void(int)> stepFeature;  ///< -1 for the previous feature in the list, +1 for the next
		std::function<bool()> canTogglePin;
		std::function<void()> togglePin;
		std::function<void(std::string_view)> openPage;  ///< Takes a MenuPageIds page ID
		std::function<bool()> canOpenEditor;
		std::function<void()> openEditor;
		std::function<void()> openHelp;
	};

	/**
	 * @brief Builds the shortcut table.
	 * @param actions The actions to bind; an entry whose action is empty is left out.
	 * @return The entries in the order the help popover lists them.
	 */
	std::vector<Shortcut> BuildTable(const Actions& actions);

	/**
	 * @brief Runs every enabled shortcut whose chord was pressed this frame.
	 * Call once per frame from inside the main menu window, only while shortcuts are allowed. Routing
	 * uses ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteFromRootWindow, so the chords fire while
	 * the main menu window or any of its child windows is focused.
	 * @param table The table from BuildTable().
	 */
	void Dispatch(const std::vector<Shortcut>& table);

	/**
	 * @brief Formats a chord for display, e.g. "Ctrl+Shift+Z" or "Ctrl+PgUp".
	 * @param chord Key plus modifiers.
	 * @return The display text.
	 */
	std::string FormatChord(ImGuiKeyChord chord);

	/**
	 * @brief The chords bound to an action, for tooltip hints.
	 * @param table The table from BuildTable().
	 * @param id A shortcut ID from Ids.
	 * @return Every chord with that ID joined by " / ", or empty when none is bound.
	 */
	std::string GetHint(const std::vector<Shortcut>& table, std::string_view id);

	/** @brief One line of the help popover's shortcut list. */
	struct HelpRow
	{
		const char* label;   ///< Translated action label
		std::string chords;  ///< Every chord for the action, joined by " / "
	};

	/**
	 * @brief The shortcut list for the help popover: one row per action, in table order.
	 * @param table The table from BuildTable().
	 */
	std::vector<HelpRow> BuildHelpRows(const std::vector<Shortcut>& table);
}
