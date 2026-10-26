#pragma once

#include <optional>
#include <string>

#include "../UndoHistory.h"
#include "RE/B/BSCoreTypes.h"  // RE::FormID
#include "SceneSettingsManager.h"

/// Scene Manager authoring UI. The panels are wired into the CS Editor; the entry
/// authoring controls are built on top of them.
namespace SceneSettingsUI
{
	/** @brief Draws the Scene Manager tab body inside a weather widget. */
	void DrawWeatherSceneTab(RE::FormID weatherId);

	/** @brief Draws the Scene Manager panel body in the CS Editor objects window. */
	void DrawSceneManagerPanel();

	/**
	 * @brief Draws the objects window's feature list, nested under the Scene Manager category entry.
	 *
	 * The panel body has no room for a feature column, so the list lives with the category that owns it.
	 * Call only while that category is selected.
	 */
	void DrawSceneManagerCategoryFeatures();

	/**
	 * @brief Draws the Locations category body: the live location chain to add from, and the user's list.
	 */
	void DrawLocationBrowser();

	/**
	 * @brief Draws one editor window per location the user opened from the browser.
	 *
	 * Call once per frame from the editor's window pass, alongside the other floating windows.
	 */
	void DrawLocationWindows();

	/** @brief Opens the CS Editor on the page authoring a scene context, with a feature selected. */
	void OpenSceneContext(const SceneSettingsManager::SceneContextId& context, const std::string& featureShortName);

	/**
	 * @brief Whether the page authoring a layer lists a feature, so opening it can select that feature.
	 * Weather pages list the transitionable set; the other layers list every scene feature.
	 */
	bool LayerListsFeature(SceneSettingsManager::SceneContextType layer, const std::string& featureShortName);

	/**
	 * @brief The context of a layer that applies where the player is now: the live period, the active
	 * weather, the interior layer, or the narrowest place in the live chain on the user's Locations list.
	 * @return Nothing when the layer cannot hold the feature or nothing matches here.
	 */
	std::optional<SceneSettingsManager::SceneContextId> ResolveCurrentLayer(
		SceneSettingsManager::SceneContextType layer, const std::string& featureShortName);

	/** @brief ResolveCurrentLayer for any feature: nothing only when no weather is active or no listed place matches. */
	std::optional<SceneSettingsManager::SceneContextId> ResolveCurrentContext(SceneSettingsManager::SceneContextType layer);

	/** @brief Opens the CS Editor on its Locations page, to add a place to the list. */
	void OpenLocationsPage();

	/** @brief Opens the CS Editor on its Scene Manager page, keeping the selected feature. */
	void OpenSceneManagerPage();

	/**
	 * @brief Ensures an override for the player's current place and opens its editor on a feature.
	 *
	 * Used when a Weather/Time-of-Day/Interior control cannot hold a setting: land on the narrowest
	 * place in the live chain with that feature selected, so the value is editable immediately.
	 * Shows a fade toast and returns false when the place cannot be opened or cannot hold the setting.
	 */
	bool OpenCurrentLocationForSetting(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/**
	 * @brief Pauses game time while a panel is on screen and restores it once none are.
	 *
	 * Call once per frame, including while the editor is closed, so the pause is always released.
	 */
	void SyncTimePause();

	/** @brief The undo target for the user's scene settings, every context at once. */
	UndoHistory::Target MakeUndoTarget();
}
