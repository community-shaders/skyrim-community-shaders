#pragma once

#include "EffectManager.h"
#include "WeatherManager.h"

/**
 * Access the single shared instance of MenuManager.
 * @returns Reference to the global MenuManager singleton.
 */

/**
 * Render the entire ImGui-based menu and its sections.
 */

/**
 * Render the UI list for available effects and effect controls.
 */

/**
 * Render the settings panel, including categorized settings sections.
 */

/**
 * Render weather-related controls and configuration UI.
 */

/**
 * Render debug controls and diagnostic UI elements.
 */

/**
 * Render all individual settings entries within the settings panel.
 */

/**
 * Produce a mapping from setting category name to the list of setting keys in that category.
 * @returns std::map where each key is a category name and the value is a vector of setting names belonging to that category.
 */

/**
 * Compute and return indices representing the currently active time-of-day entries.
 * @returns Vector of indices corresponding to active time-of-day entries.
 */

/**
 * Compute the blend factor for a given time-of-day entry index.
 * @param timeIndex Index of the time-of-day entry to evaluate.
 * @returns Blend factor in the range [0, 1] for the specified time index.
 */
class MenuManager
{
public:
	static MenuManager& GetSingleton();

	// Main UI rendering method
	void RenderImGui();

private:
	// UI section rendering methods
	void RenderEffectsList();
	void RenderSettingsPanel();
	void RenderWeatherControl();
	void RenderDebugControl();

	// Helper UI methods
	void RenderAllSettings();
	std::map<std::string, std::vector<std::string>> GetCategorizedSettings() const;
	std::vector<int> GetActiveTimeOfDayIndices() const;
	float GetTimeOfDayBlendFactor(int timeIndex) const;
};