#pragma once

#include "Menu/SettingsSections.h"
#include "Utils/Input.h"
#include <functional>
#include <span>
#include <vector>

// Forward declarations
class Menu;

/**
 * @brief Draws the General and Appearance sections of the Settings page (see SettingsPageRenderer).
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
		bool& settingToggleKey;          /**< @brief True while capturing a new menu toggle key. */
		bool& settingsEffectsToggle;     /**< @brief True while capturing a new effects toggle key. */
		bool& settingSkipCompilationKey; /**< @brief True while capturing a new skip-compilation key. */
		bool& settingOverlayToggleKey;   /**< @brief True while capturing a new overlay toggle key. */
		bool& settingShaderBlockPrevKey; /**< @brief True while capturing a new shader-block-previous key. */
		bool& settingShaderBlockNextKey; /**< @brief True while capturing a new shader-block-next key. */
		bool& settingCSEditorToggleKey;  /**< @brief True while capturing a new CS Editor toggle key. */
		bool& settingScreenshotKey;      /**< @brief True while capturing a new screenshot key. */
		bool& settingEffects11ToggleKey; /**< @brief True while capturing a new Effects 11 toggle key. */
		bool& settingEffects11EditorKey; /**< @brief True while capturing a new Effects 11 editor key. */
	};

	/** @brief Searchable setting labels of the sections drawn here. */
	static std::span<const SettingsSearchEntry> GetSearchIndex();

	/** @brief The one line explaining how theme edits and the theme selection are saved. */
	/** @brief The theme save note: one subtitle-sized line in the info colour, for the header of sections that edit the theme. */
	static void RenderThemeSaveNote();

	/** @brief Shader cache options and the last build's compilation breakdown. */
	static void RenderShadersSection();
	/**
	 * @brief Rebinding rows for the menu hotkeys.
	 * @param state References to the key-capture state flags for all rebindable hotkeys.
	 */
	static void RenderKeybindingsSection(
		SettingsState& state);
	/** @brief Interface language picker. */
	static void RenderLanguageSection();
	/** @brief Menu behaviour options and the Reset Window Layout button. */
	/** @brief General interface options (animations, window and layout, tooltips); drawn on the Styling page's General tab. */
	static void RenderInterfaceOptions();
	/** @brief Header title and icon options, background blur, menu fade and the theme cursor. */
	static void RenderHeaderEffectsSection();
	/** @brief Theme preset picker with save, save-as and delete. */
	static void RenderThemesSection();
	/** @brief Font size and per-role font pickers. Requests the font preview atlas while drawn. */
	static void RenderFontsSection();
	/** @brief Global scale and scrollbar opacity, with the raw ImGui style sliders in a collapsed fold. */
	static void RenderStylingSection();
	/** @brief Per-role look of the menu surfaces (Home cards, toasts, status card, palette, selected nav row). */
	static void RenderSurfacesSection();
	/** @brief Simple palette and the full ImGui color list. */
	static void RenderColorsSection();
};
