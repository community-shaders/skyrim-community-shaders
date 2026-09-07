#pragma once

#include "RE/B/BSCoreTypes.h"  // RE::FormID

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

	/**
	 * @brief Pauses game time while a panel is on screen and restores it once none are.
	 *
	 * Call once per frame, including while the editor is closed, so the pause is always released.
	 */
	void SyncTimePause();
}
