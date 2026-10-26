#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Renders the Home page of the in-game menu and the first-time setup dialog.
 *
 * Home is a status dashboard: a card grid with the active preset, the scene and CS Editor, the shader
 * cache, what needs attention, quick access to features and the community links. The FAQ entries are
 * drawn for the header's help popover. Also manages the first-time setup dialog shown on initial launch.
 */
class HomePageRenderer
{
public:
	// Constants
	static constexpr float HOTKEY_TEXT_SCALE = 1.6f;
	static constexpr float HOTKEY_TEXT_SCALE_CAPTURING = 2.0f;
	static constexpr float HOTKEY_HOVER_DIM_FACTOR = 0.7f;
	static constexpr float HELP_TEXT_SCALE = 1.35f;
	static constexpr float LOGO_WATERMARK_HEIGHT = 156.0f;
	static constexpr uint8_t MODAL_OVERLAY_ALPHA = 160;

	// First-time setup dialog layout (1080p baseline, scaled by GetUIScale)
	static constexpr float DIALOG_MIN_WIDTH = 390.0f;
	static constexpr float DIALOG_MAX_WIDTH = 470.0f;
	static constexpr float DIALOG_CORNER_ROUNDING = 6.0f;
	static constexpr float DIALOG_LINE_TIGHTEN = 3.0f;

	/** @brief Renders the Home dashboard: an unsaved-changes banner when needed, then the card grid. */
	static void RenderHomePage();

	// First-time setup management
	/** @brief Returns true if the first-time setup dialog should be displayed to the user. */
	static bool ShouldShowFirstTimeSetup();

	/** @brief Renders the modal first-time setup dialog overlay with initial configuration options. */
	static void RenderFirstTimeSetupDialog();

	/** @brief Dismisses the first-time setup dialog and persists completion. Called by the menu toggle key. */
	static void MarkFirstTimeSetupComplete();

	/** @brief Draws the FAQ questions as collapsible answers, without the section title. The header's help popover shows them in a modal. */
	static void RenderFAQEntries();

private:
	// State
	static bool isFirstTimeSetupShown;
};
