#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "Menu/Icons/helpers/IconFonts.h"

struct Feature;

/**
 * @brief Canonical, untranslated IDs of the built-in menu pages.
 *
 * Page logic compares these rather than the translated labels, so it works in every locale. Pass one to
 * Menu::SelectFeatureMenu() to open that page.
 */
namespace MenuPageIds
{
	inline constexpr std::string_view kHome = "Home";
	inline constexpr std::string_view kPresets = "Presets";
	inline constexpr std::string_view kSettings = "Settings";
	/// Alias: opens Settings at its first General section (General and Advanced are part of Settings)
	inline constexpr std::string_view kGeneral = "General";
	/// Alias: opens Settings at its first Advanced section
	inline constexpr std::string_view kAdvanced = "Advanced";
	inline constexpr std::string_view kProfiling = "Profiling";
	inline constexpr std::string_view kCSEditor = "CS Editor";
	inline constexpr std::string_view kFeatureIssues = "Feature Issues";
}

/**
 * @brief Filter chips of the main menu's feature list, stored as a bitmask in Menu::Settings::FeatureListFilter.
 *
 * Chips combine with AND, and the search text applies on top. Without kUnloaded only loaded features are listed;
 * with it, only the ones that are not.
 */
namespace FeatureListFilter
{
	inline constexpr std::uint32_t kFavorites = 1u << 0;   ///< Pinned features
	inline constexpr std::uint32_t kModified = 1u << 1;    ///< Settings differ from defaults and Baseline presets
	inline constexpr std::uint32_t kScene = 1u << 2;       ///< Has Scene Manager settings
	inline constexpr std::uint32_t kPreRelease = 1u << 3;  ///< Alpha or beta
	inline constexpr std::uint32_t kUnloaded = 1u << 4;    ///< Not loaded: missing, failed, or disabled at boot
	/// Every chip; a saved mask is cut to these bits.
	inline constexpr std::uint32_t kKnown = kFavorites | kModified | kScene | kPreRelease | kUnloaded;
}

/**
 * @brief Renders the two-column feature list and settings panel in the main menu.
 *
 * The left column shows a searchable, categorized list of built-in pages and
 * installed features. The right column displays the settings UI for whichever
 * item is currently selected.
 */
class FeatureListRenderer
{
public:
	/** @brief How a built-in page's row stands out in the list. */
	enum class NavStyle : std::uint8_t
	{
		Plain,     ///< An ordinary row
		Accent,    ///< Filled pill in the info colour, for the page most users want first (Presets)
		Launcher,  ///< Outlined pill with an external-open glyph: opens a window rather than a page (CS Editor)
		Alert,     ///< Error-coloured row, for problems that need attention
	};

	/** @brief Describes a built-in (non-feature) menu page with an ID, a display name and a draw callback. */
	struct BuiltInMenu
	{
		std::string_view id;  ///< Canonical page ID from MenuPageIds, used for all page logic
		std::string name;     ///< Translated label shown in the list
		std::function<void()> func;
		Icons::GlyphRef icon{};  ///< Glyph leading the row and the page header; none leaves the space empty
		NavStyle style = NavStyle::Plain;
		std::string hint;         ///< Muted line under an Accent or Launcher row's label, and the page header's subtitle
		bool showHeader = false;  ///< Draws a BrowserUI::PageHeader above the page; off for pages with their own title
	};

	/** @brief Represents a collapsible category header in the feature list. */
	struct CategoryHeader
	{
		std::string name;
	};

	/** @brief Variant type representing any entry in the menu list. */
	using MenuFuncInfo = std::variant<BuiltInMenu, std::string, CategoryHeader, Feature*>;

	/** @brief Main-menu state the list and settings panel read and report back, passed in by Menu each frame. */
	struct Hooks
	{
		/// In: moves the selection to the previous (< 0) or next (> 0) feature in the visible list, skipping
		/// category headers, labels and built-in pages. Reset to 0 once applied.
		int featureStep = 0;
		/// In: focuses the feature search field this frame (Ctrl+F). Reset once applied.
		bool focusSearch = false;
		/// Called with the feature whose page is about to be drawn this frame, before its header.
		std::function<void(Feature*)> onFeatureShown;
		/// Runs a header command that rewrites a feature's settings (Restore Defaults, Use Baseline Values) so it can
		/// be undone. When empty the command just runs.
		std::function<void(Feature*, const std::function<void()>&)> runFeatureCommand;
		/// In: the chord that collapses and expands the list (Ctrl+B), for tooltips; empty when unbound.
		std::string toggleListShortcut;
	};

	/** @brief How many features each filter chip would list on its own, ignoring the search and other chips. */
	struct FilterCounts
	{
		int favorites = 0;
		int modified = 0;
		int scene = 0;
		int preRelease = 0;
		int unloaded = 0;
		/// Features the current search and chips list, pinned ones included.
		int shown = 0;
	};

	/**
	 * @brief Renders the full two-column feature list and settings panel.
	 *
	 * Builds the menu list from built-in pages and loaded features, handles
	 * pending feature selection requests, then draws the left-column navigation
	 * and right-column settings content.
	 *
	 * @param footerHeight Height reserved for the footer area below the list.
	 * @param selectedMenu Index of the currently selected menu item (updated on selection change).
	 * @param featureSearch Current search filter string (updated by the search input).
	 * @param pendingFeatureSelection Feature short name or page ID (MenuPageIds) to auto-select; a page display
	 *        name is accepted as a fallback. Cleared after processing.
	 * @param categoryExpansionStates Map of category name to expanded/collapsed state.
	 * @param drawSettings Callback that renders the Settings page content.
	 * @param hooks Requests from the menu and callbacks into it; requests are reset once applied.
	 */
	static void RenderFeatureList(
		float footerHeight,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::string& pendingFeatureSelection,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawSettings,
		Hooks& hooks);

	/**
	 * @brief Pins a loaded feature to the top of the list, or unpins it, keeping it selected. This is what the
	 * feature page's pin button does.
	 * @param feat A loaded feature.
	 */
	static void ToggleFavorite(Feature* feat);

	/**
	 * @brief Draws a loaded feature's settings under the baseline interceptor scope and queues new constraint warnings.
	 * @param feat A loaded feature.
	 */
	static void DrawFeatureBody(Feature* feat);

	/**
	 * @brief Draws the reactive constraint warning modal while one is pending; call once per frame, outside child windows.
	 * @param pendingFeatureSelection Receives the short name of a feature the user navigates to from the dialog.
	 */
	static void DrawConstraintWarningDialog(std::string& pendingFeatureSelection);

	/** @brief Translated display label for a FeatureCategories value. */
	static std::string TranslateCategory(std::string_view category);

private:
	struct ListMenuVisitor
	{
		size_t listId;
		size_t& selectedMenuRef;
		std::map<std::string, bool>& categoryExpansionStates;
		const Hooks& hooks;

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string& label);
		void operator()(const CategoryHeader& header);
		void operator()(Feature* feat);

	private:
		/** @brief Right-click menu of a feature row: pin, restore defaults, load at boot, copy name. */
		void RenderRowContextMenu(Feature* feat) const;
	};

	struct DrawMenuVisitor
	{
		DrawMenuVisitor(std::string& pendingFeatureSelectionRef, const Hooks& hooksRef) :
			pendingFeatureSelection(pendingFeatureSelectionRef), hooks(hooksRef) {}

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string&);
		void operator()(const CategoryHeader&);
		void operator()(Feature* feat);

	private:
		std::string& pendingFeatureSelection;
		const Hooks& hooks;

		// Helper methods for Feature rendering
		void RenderFeatureHeader(Feature* feat, bool isDisabled, bool isLoaded);
		void RenderFeatureSettings(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage);
		/**
		 * @brief The header's overflow menu: Export Overwrite, Use Baseline Values and Restore Defaults. Entries
		 * that cannot run stay listed, greyed, with a tooltip saying why.
		 */
		void RenderActionsMenu(Feature* feat, bool canExport, bool canRestore, bool canUseBaselines);
		/** @brief Centred "disabled at boot" notice with a button that turns Load at Boot back on. */
		static void RenderDisabledAtBootNotice(Feature* feat);
		/** @brief Runs a command that rewrites the feature's settings through Hooks::runFeatureCommand. */
		void RunFeatureCommand(Feature* feat, const std::function<void()>& command) const;
		static void RenderFavoriteButton(Feature* feat);
	};

	/**
	 * @brief Builds the list from the built-in pages and the features the search and filter chips pass.
	 * @param counts Receives each chip's count and the number of features listed.
	 */
	static std::vector<MenuFuncInfo> BuildMenuList(
		const std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawSettings,
		FilterCounts& counts);

	/**
	 * @brief Rebuilds the cached list when something it depends on changed: the search, chips, sort order,
	 * language, favorites, a category's expansion, feature edits, scene entries, or the menu was closed.
	 * @return True when the list was rebuilt, so the caller can re-find the selected entry.
	 */
	static bool UpdateMenuList(
		const std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawSettings);

	/** @brief Draws the filter chips under the search field and handles their clicks. */
	static void RenderFilterChips(const FilterCounts& counts);

	static void HandlePendingFeatureSelection(
		std::string& pendingFeatureSelection,
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu);

	/** @brief Draws the expanded list: fixed top pages, the searchable feature list, then fixed bottom pages. */
	static void RenderLeftColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		Hooks& hooks);

	/** @brief Draws the thin strip a collapsed list leaves; clicking it expands the list. */
	static void RenderCollapsedStrip(const Hooks& hooks);

	/** @brief Draws a built-in page's row; the CS Editor row opens its window instead of selecting the page. */
	static void RenderBuiltInRow(const BuiltInMenu& menu, size_t listId, size_t& selectedMenu);

	static void RenderRightColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t selectedMenu,
		std::string& pendingFeatureSelection,
		const Hooks& hooks);
};