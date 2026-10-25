#pragma once

#include "Menu/SettingsTabRenderer.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

/**
 * @brief Renders the Settings page of the main menu.
 *
 * A fixed-width sub-nav on the left lists the sections under General, Appearance and Advanced group labels;
 * the selected section draws in a scrolling pane on the right. The page has one navigation level only:
 * sections use labels for their parts, never tab bars.
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
	 * @brief Draws the sub-nav and the selected section.
	 * @param state Menu state; an unknown section ID is replaced by the first section.
	 */
	static void Render(PageState& state);

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
