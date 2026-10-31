#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Widget;

namespace Browser
{
	/** @brief Which field the form-list filter text is matched against. */
	enum class FilterScope : int
	{
		All = 0,
		EditorID,
		FormID,
		File,
		Status,
		Count
	};

	/** @brief Column the form list is sorted by. None keeps the game's load order. */
	enum class SortKey : int
	{
		None,
		EditorID,
		FormID,
		File,
		Status,
		Saved
	};

	/** @brief One list row, with the strings it is filtered, sorted and drawn by computed once per rebuild. */
	struct FormRow
	{
		Widget* widget = nullptr;
		RE::FormID formId = 0;
		std::string editorId;
		/// What the Name column shows: the editor ID, or the local form ID when the game has none.
		std::string display;
		std::string formIdHex;
		std::string file;
		/// Status marker name, empty when unmarked.
		std::string marker;
		bool favorite = false;
		bool hasSavedFile = false;
		bool unsaved = false;
		/// The active preset pack has a file for the form; the badge fields come from Widget::GetPackBadge.
		bool fromPack = false;
		ImVec4 packBadgeColor{};
		std::string packBadgeTooltip;
	};

	/** @brief A weather that references the inspected record, and in which of its slots. */
	struct WeatherUsage
	{
		Widget* weather = nullptr;
		std::string slots;
	};

	/** @brief Everything the form-list page remembers between frames. Owned by EditorWindow. */
	struct FormListState
	{
		// Filters; reset whenever the category changes so a stale scope cannot hide every row.
		char filter[256] = {};
		FilterScope scope = FilterScope::All;
		bool onlyFavorites = false;
		bool onlyFlagged = false;
		bool onlySaved = false;
		bool onlyUnsaved = false;
		bool onlyPreset = false;
		/// Plugin filename to show, empty for every plugin.
		std::string plugin;

		SortKey sortKey = SortKey::None;
		bool sortAscending = true;

		/// Share of the page height the list keeps when the inspector sits under it.
		static constexpr float kDefaultStackedRatio = 0.55f;
		float stackedRatio = kDefaultStackedRatio;

		/// Filtered and sorted rows for the current category.
		std::vector<FormRow> rows;
		/// Fingerprint of the inputs `rows` was built from; a mismatch triggers a rebuild.
		std::string viewKey;
		double builtAt = -1.0;
		/// Bumped whenever favourites, markers or saved files change outside the filter inputs.
		unsigned dataGeneration = 0;

		// Category-wide totals shown on the filter chips.
		int total = 0;
		int favoriteCount = 0;
		int flaggedCount = 0;
		int savedCount = 0;
		int unsavedCount = 0;
		int presetCount = 0;
		/// Plugins present in the category, with their record counts, sorted by name.
		std::vector<std::pair<std::string, int>> plugins;

		/// Selected record per category, so switching back restores it.
		std::unordered_map<std::string, RE::FormID> selectedByCategory;
		/// Set when a selection is made from outside the list (links, chips); the list scrolls to it once.
		bool scrollToSelection = false;

		/// Reverse lookup for the inspector, recomputed when the inspected record changes.
		RE::FormID usageFor = 0;
		std::string usageCategory;
		std::vector<WeatherUsage> usage;
		/// The `builtAt` the usage was computed against, so it refreshes with the list.
		double usageBuiltAt = -1.0;

		/** @brief Clears every filter. */
		void ResetFilters()
		{
			filter[0] = '\0';
			scope = FilterScope::All;
			onlyFavorites = onlyFlagged = onlySaved = onlyUnsaved = onlyPreset = false;
			plugin.clear();
		}

		/** @brief Forces the rows to be rebuilt on the next frame. */
		void Invalidate() { ++dataGeneration; }
	};
}
