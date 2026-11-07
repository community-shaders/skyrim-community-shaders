#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Renders the Home page of the in-game menu and the first-time setup dialog.
 *
 * Home is a status dashboard: a card grid with the active preset, the scene and CS Editor, the shader
 * cache, what needs attention, quick access to features and the community links. Also manages the first-time setup dialog shown on initial launch.
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

	/** @brief True while the Home dashboard was drawn this frame or the last, for the header's Home button highlight. */
	static bool IsShowing();

	/** @brief Opens the keyboard shortcuts and help popover on the next frame it is drawn (F1). */
	static void RequestHelpPopover();

	/** @brief Width the status bar's help button takes, for laying out the row around it. */
	static float GetHelpButtonWidth();

	/**
	 * @brief Draws the small ? button at the cursor, plus the shortcuts and help popover it opens. The status bar
	 * calls it at the right end of its row; F1 opens the popover through it too.
	 */
	static void RenderHelpButton();

	/**
	 * @brief Opens Home scrolled to the Needs Attention card and outlines it briefly.
	 * Used by the header and footer issues indicators and by the Issues page ID.
	 */
	static void FocusNeedsAttention();

	/** @brief True while the Needs Attention outline from FocusNeedsAttention is still showing. */
	static bool IsAttentionFocused();

	/** @brief Seconds left on the Needs Attention outline; zero or negative when it is over. */
	static double GetAttentionFocusRemaining();

	/** @brief Renders the Home dashboard: an unsaved-changes banner when needed, then the card grid. */
	static void RenderHomePage();

	// First-time setup management
	/** @brief Returns true if the first-time setup dialog should be displayed to the user. */
	static bool ShouldShowFirstTimeSetup();

	/** @brief Renders the modal first-time setup dialog overlay with initial configuration options. */
	static void RenderFirstTimeSetupDialog();

	/** @brief Dismisses the first-time setup dialog and persists completion. Called by the menu toggle key. */
	static void MarkFirstTimeSetupComplete();

private:
	static void RenderHelpPopover();

	// State
	static bool isFirstTimeSetupShown;
	static int lastShownFrame;
	static bool helpPopoverRequested;
	static bool attentionScrollPending;
	static double attentionFocusUntil;
};
