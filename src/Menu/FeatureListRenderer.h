#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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
	inline constexpr std::string_view kGeneral = "General";
	inline constexpr std::string_view kAdvanced = "Advanced";
	inline constexpr std::string_view kProfiling = "Profiling";
	inline constexpr std::string_view kCSEditor = "CS Editor";
	inline constexpr std::string_view kFeatureIssues = "Feature Issues";
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
	/** @brief Describes a built-in (non-feature) menu page with an ID, a display name and a draw callback. */
	struct BuiltInMenu
	{
		std::string_view id;  ///< Canonical page ID from MenuPageIds, used for all page logic
		std::string name;     ///< Translated label shown in the list
		std::function<void()> func;
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
		/// Called with the feature whose page is about to be drawn this frame, before its header.
		std::function<void(Feature*)> onFeatureShown;
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
	 * @param drawGeneralSettings Callback that renders the General settings page content.
	 * @param drawAdvancedSettings Callback that renders the Advanced settings page content.
	 * @param hooks Requests from the menu and callbacks into it; requests are reset once applied.
	 */
	static void RenderFeatureList(
		float footerHeight,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::string& pendingFeatureSelection,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings,
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

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string& label);
		void operator()(const CategoryHeader& header);
		void operator()(Feature* feat);
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
		/** @brief Drops the feature's edits to settings its Baseline packs set. */
		static void RenderUseBaselinesButton(Feature* feat);
		static void RenderRestoreDefaultsButton(Feature* feat);
		static void RenderFavoriteButton(Feature* feat);
	};

	static std::vector<MenuFuncInfo> BuildMenuList(
		const std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

	static void HandlePendingFeatureSelection(
		std::string& pendingFeatureSelection,
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu);

	static void RenderLeftColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates);

	static void RenderRightColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t selectedMenu,
		std::string& pendingFeatureSelection,
		const Hooks& hooks);
};