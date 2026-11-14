#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Overlays
{
	struct RowInfo;
}

/** @brief The "Preset Overlays" editor panel, and the overlay lines shared by row tooltips. */
class OverlayPanel
{
public:
	/** @brief Appended to the label of a row an active overlay changes. */
	static constexpr const char* kOverlaidSuffix = " *";

	/** @brief Editor hooks, since the panel cannot reach Effects11Editor's private methods. */
	struct Callbacks
	{
		std::function<void()> placeWindow;                   ///< Positions the window before it begins
		std::function<void(std::string&, bool&)> searchBar;  ///< Draws the editor's search field
		std::function<void()> revert;                        ///< Reloads every value from disk
	};

	/** @brief Tooltip lines naming the overlays that change a row, and why it is locked. */
	static void DrawRowTooltip(const Overlays::RowInfo& a_row);

	/**
	 * @brief Draws the window.
	 * @param a_open Cleared when the user closes the window.
	 * @param a_dirty Set when an overlay edit needs saving.
	 */
	void Draw(bool& a_open, bool& a_dirty, const Callbacks& a_callbacks);

private:
	/** @brief The "Edits go to" combo and its warnings. */
	void DrawEditTarget();
	/** @brief The filtered overlays under collapsible group headers. */
	void DrawOverlayList();
	/** @brief One overlay's checkbox, tooltip and Amount slider. */
	void DrawOverlayRow(size_t a_index);
	/** @brief An overlay's name, file, description, incompatibilities and unused parameter count. */
	void DrawOverlayTooltip(size_t a_index);
	/** @brief The active overlays in apply order, with move up and down buttons. */
	void DrawActiveOrder();
	/** @brief The "New overlay" and "New overlay from unsaved changes" buttons. */
	void DrawCreateButtons();
	/** @brief The modal that names a new overlay and creates it. */
	void DrawNamePopup(bool& a_dirty, const Callbacks& a_callbacks);
	/** @brief Rebuilds the filtered, grouped order when the overlays or the search changed. */
	void RefreshOrder();

	std::string filter;
	bool focusSearch = false;
	std::vector<size_t> order;
	std::string orderFilter;
	uint32_t orderGeneration = UINT32_MAX;
	std::string pendingName;
	bool pendingFromChanges = false;
	bool openNamePopup = false;
};
