#pragma once

#include <functional>

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
		bool& settingShaderBlockPrevKey;  // Debug: shader block previous key
		bool& settingShaderBlockNextKey;  // Debug: shader block next key
	};

	static void RenderGeneralSettings(
		SettingsState& state,
		const std::function<const char*(uint32_t)>& keyIdToString);

private:
	static void RenderShadersTab();
	static void RenderKeybindingsTab(
		SettingsState& state,
		const std::function<const char*(uint32_t)>& keyIdToString);
	static void RenderInterfaceTab();

	// Interface sub-tabs
	static void RenderThemesTab();
	static void RenderFontsTab();
	static void RenderStylingTab();
	static void RenderColorsTab();
};