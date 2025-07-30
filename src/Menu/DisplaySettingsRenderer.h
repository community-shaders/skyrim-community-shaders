#pragma once

#include <functional>
#include <string>
#include <vector>

/**
 * @brief Specialized renderer component for display and upscaling settings
 *
 * This class was extracted from Menu.cpp to handle rendering of display-related
 * settings including upscaling, frame generation, and related graphics options.
 * It provides a clean separation between the Menu coordinator and display settings
 * presentation logic.
 *
 * The renderer uses callback functions to access feature state and drawing routines
 * while maintaining loose coupling with the rest of the Menu system.
 */
class DisplaySettingsRenderer
{
public:
	static void RenderDisplaySettings(
		bool upscalerLoaded,
		const std::function<bool(const std::string&)>& isFeatureDisabled,
		const std::function<void()>& drawUpscalingSettings);

private:
	static void RenderFeatureSection(
		const std::string& featureName,
		const std::function<void()>& drawFunc,
		bool isDisabled,
		bool isVRMode);
};