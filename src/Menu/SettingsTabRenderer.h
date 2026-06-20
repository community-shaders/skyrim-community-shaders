#pragma once

#include "Utils/Input.h"
#include <functional>
#include <vector>

// Forward declarations
class Menu;

/**
 * @brief Renders the General Settings page with Shaders, Keybindings, and Interface tabs.
 *
 * The Shaders tab shows shader compilation options. The Keybindings tab allows
 * rebinding hotkeys for menu toggle, effects toggle, and other actions. The
 * Interface tab provides theme, font, styling, color, and behavior configuration.
 */
class SettingsTabRenderer
{
public:
	/**
	 * @brief References to key-capture state flags managed by the Menu class.
	 *
	 * Each boolean reference indicates whether the corresponding keybinding
	 * slot is actively waiting for the user to press a new key.
	 */
	struct SettingsState
	{
		bool& settingToggleKey;               /**< @brief True while capturing a new menu toggle key. */
		bool& settingsEffectsToggle;          /**< @brief True while capturing a new effects toggle key. */
		bool& settingSkipCompilationKey;      /**< @brief True while capturing a new skip-compilation key. */
		bool& settingOverlayToggleKey;        /**< @brief True while capturing a new overlay toggle key. */
		bool& settingShaderBlockPrevKey;      /**< @brief True while capturing a new shader-block-previous key. */
		bool& settingShaderBlockNextKey;      /**< @brief True while capturing a new shader-block-next key. */
		bool& settingCSEditorToggleKey;       /**< @brief True while capturing a new CS Editor toggle key. */
		bool& settingScreenshotKey;           /**< @brief True while capturing a new screenshot key. */
	};

	/**
	 * @brief Renders the General Settings page with tabbed sub-sections.
	 *
	 * Draws a tab bar containing Shaders, Keybindings, and Interface tabs.
	 * The Interface tab further contains sub-tabs for Behavior, Themes, Fonts,
	 * Styling, and Colors.
	 *
	 * @param state References to the key-capture state flags for all rebindable hotkeys.
	 */
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
