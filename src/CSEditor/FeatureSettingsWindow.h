#pragma once

#include <string>

/// The CS Editor's Base Settings window: every feature's own settings, the same as the main menu's
/// feature pages, with Post Processing pinned on top. Scene layers override these values.
namespace FeatureSettingsWindow
{
	/**
	 * @brief Selects a feature in the window and brings it to the front. The caller shows the window.
	 * @param featureShortName Feature whose base settings to show.
	 */
	void Select(const std::string& featureShortName);

	/**
	 * @brief Draws the Base Settings window and its constraint warning.
	 * @param open Window visibility; cleared by its close button, set when the warning navigates to a feature.
	 */
	void Draw(bool& open);
}
