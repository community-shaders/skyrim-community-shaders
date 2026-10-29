#pragma once

#include "Menu/SettingsTabRenderer.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Renders the Settings page of the main menu.
 *
 * The main menu's left pane lists the sections under General, Appearance and Advanced group labels (RenderSectionList),
 * in place of the feature list; the selected section draws in the page
 * pane (Render). The page has one navigation level only: sections use labels for their parts, never tab bars.
 */
class SettingsPageRenderer
{
public:
	/** @brief Menu state the page reads and updates. It lives in Menu and is passed in every frame. */
	struct PageState
	{
		std::string& section;                     ///< Selected section ID (SettingsSectionIds)
		SettingsTabRenderer::SettingsState keys;  ///< Key-capture flags for the Keybindings section
		std::function<void()> drawDisableAtBoot;  ///< Draws Menu's disable-at-boot list
	};

	/**
	 * @brief Draws the selected section.
	 * @param state Menu state; an unknown section ID is replaced by the first section.
	 */
	static void Render(PageState& state);

	/**
	 * @brief Draws the section list, for the main menu's left pane. Selecting a
	 * section updates state.section.
	 * @param state Menu state; an unknown section ID is replaced by the first section.
	 */
	static void RenderSectionList(PageState& state);

	/** @brief A section's ID with its translated title and group label, for lists outside the page. */
	struct SectionInfo
	{
		std::string_view id;  ///< SettingsSectionIds value
		const char* title;    ///< Translated section title
		const char* group;    ///< Translated group label (General, Appearance, Advanced)
	};

	/** @brief Every section in sub-nav order. */
	static std::vector<SectionInfo> GetSections();

	/** @brief True when id names a Settings section. */
	static bool IsKnownSection(std::string_view id);

	/** @brief ID of the first section, the fallback for unknown or missing IDs. */
	static std::string_view GetFirstSection();

	/**
	 * @brief Section a legacy page ID opens now that General and Advanced are part of Settings.
	 * @param pageId A page ID passed to Menu::SelectFeatureMenu().
	 * @return The first General section for MenuPageIds::kGeneral, the first Advanced section for
	 *         MenuPageIds::kAdvanced, otherwise std::nullopt.
	 */
	static std::optional<std::string_view> GetAliasSection(std::string_view pageId);
};
