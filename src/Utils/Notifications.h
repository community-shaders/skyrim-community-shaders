#pragma once

#include <imgui.h>
#include <string>

/**
 * @brief Temporary toast cards stacked at the bottom centre of the screen, shared by the CS Editor
 * and the main menu. Each card fades in, stays for its duration and fades out.
 */
namespace Util::Notifications
{
	/** @brief How long a card stays on screen when the caller does not say, in seconds. */
	inline constexpr float kDefaultDuration = 3.0f;

	/**
	 * @brief Queues a card. Ignored, with a log line, before ImGui is initialised.
	 * @param message The text to display.
	 * @param color Text and accent colour.
	 * @param duration How long the card stays on screen, in seconds.
	 */
	void Show(const std::string& message, const ImVec4& color, float duration = kDefaultDuration);

	/** @brief Show(), unless a card with the same message is still queued, so repeats do not stack. */
	void ShowOnce(const std::string& message, const ImVec4& color, float duration = kDefaultDuration);

	/** @brief True while a card is still queued, so the overlay keeps drawing until it is gone. */
	bool HasAny();

	/**
	 * @brief Expires old cards and draws the rest on top of the current frame's windows. Only the
	 * first call each frame draws, so several surfaces can host it without drawing cards twice.
	 */
	void Draw();
}
