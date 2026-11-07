#pragma once

#include "Utils/Input.h"
#include <functional>
#include <vector>

class Menu;

/**
 * @brief Specialized renderer component for overlay and frame management
 *
 * Handles shader compilation status, feature overlays, A/B testing and the ImGui frame
 * lifecycle. A stateless utility: Menu functionality is reached through callbacks.
 */
class OverlayRenderer
{
public:
	/**
	 * @brief Main overlay rendering entry point
	 *
	 * Coordinates all overlay rendering activities including input processing,
	 * shader compilation status display, feature overlays, A/B testing, and ImGui frame
	 * management. Uses callback functions to access Menu functionality while maintaining
	 * architectural separation.
	 *
	 * @param menu Reference to the Menu instance for accessing settings and state
	 * @param processInputEventQueue Callback to process queued input events
	 * @param drawSettings Callback to render the main settings interface
	 * @param keyIdToString Callback to convert key codes to human-readable strings
	 * @param cachedFontSize Current cached font size for reload detection
	 * @param currentFontSize Current font size setting for comparison
	 */
	static void RenderOverlay(
		Menu& menu,
		const std::function<void()>& processInputEventQueue,
		const std::function<void()>& drawSettings,
		const std::function<const char*(std::vector<InputCombo>)>& keyIdToString,
		float& cachedFontSize,
		float currentFontSize);

private:
	static bool ShouldSkipRendering();
	static void HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize);
	static void InitializeImGuiFrame(Menu& menu);
	static void RenderShaderCompilationStatus(const std::function<const char*(std::vector<InputCombo>)>& keyIdToString);
	static void RenderShaderBlockingStatus();
	static void RenderFirstTimeSetupOverlay();
	static void RenderFeatureOverlays();
	static void HandleABTesting();
	static void FinalizeImGuiFrame();
};