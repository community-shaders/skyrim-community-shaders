#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Renders the Home page of the in-game menu with welcome info, FAQ, and first-time setup.
 *
 * Displays a welcome banner, quick links (Discord, etc.), active constraint
 * warnings, and a FAQ section. Also manages the first-time setup dialog
 * shown on initial launch.
 */
class HomePageRenderer
{
public:
	// Constants
	static constexpr const char* DISCORD_URL = "https://discord.com/invite/nkrQybAsyy";
	static constexpr float TITLE_FONT_SCALE = 2.0f;
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

	// Discord banner scaling constants
	static constexpr float DISCORD_BANNER_TARGET_WIDTH_RATIO = 0.85f;
	static constexpr float DISCORD_BANNER_MIN_WIDTH = 150.0f;
	static constexpr float DISCORD_BANNER_MAX_WIDTH = 1200.0f;
	static constexpr float DISCORD_BANNER_PADDING_MARGIN = 40.0f;

	/** @brief Renders the full Home page including welcome, quick links, constraints, and FAQ sections. */
	static void RenderHomePage();

	// First-time setup management
	/** @brief Returns true if the first-time setup dialog should be displayed to the user. */
	static bool ShouldShowFirstTimeSetup();

	/** @brief Renders the modal first-time setup dialog overlay with initial configuration options. */
	static void RenderFirstTimeSetupDialog();

	/**
	 * @brief Checks whether a key release event should be suppressed.
	 *
	 * Returns true and clears internal state if the given key was the one
	 * used to close the first-time setup dialog, preventing the release
	 * from triggering the menu toggle.
	 *
	 * @param key The virtual key code of the released key.
	 * @return true if the key release should be consumed, false otherwise.
	 */
	static bool ShouldSkipKeyRelease(uint32_t key);

private:
	static void RenderWelcomeSection();
	static void RenderQuickLinksSection();
	static void RenderFAQSection();
	static void RenderActiveConstraintsSection();

	static void MarkFirstTimeSetupComplete(uint32_t closingKey);

	// State
	static bool isFirstTimeSetupShown;
	static uint32_t keyThatClosedDialog;
};
