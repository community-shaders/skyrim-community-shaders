#pragma once

#include "Utils/Input.h"
#include <functional>
#include <vector>

// Forward declarations
class Menu;

class SettingsTabRenderer
{
public:
	// Settings state passed from Menu
	struct SettingsState
	{
		bool& settingToggleKey;
		bool& settingsEffectsToggle;
		bool& settingSkipCompilationKey;
		bool& settingOverlayToggleKey;
		bool& settingShaderBlockPrevKey;      // Debug: shader block previous key
		bool& settingShaderBlockNextKey;      // Debug: shader block next key
		bool& settingWeatherEditorToggleKey;  // Weather Editor toggle key
		bool& settingScreenshotKey;           // Screenshot capture key
	};

	static void RenderGeneralSettings(
		SettingsState& state);

private:
	static void RenderShadersTab();
	static void RenderKeybindingsTab(
		SettingsState& state);
	static void RenderInterfaceTab();

	// Interface sub-tabs
	static void RenderBehaviorTab();
	static void RenderThemesTab();
	static void RenderFontsTab();
	static void RenderStylingTab();
	static void RenderColorsTab();
};