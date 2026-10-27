#pragma once

#include <imgui.h>

/**
 * @brief The menu's one flat surface style: a fill a subtle step above the window colour, a hairline border and, for
 * floating things only, a single soft shadow. Everything derives from style colours so every theme works.
 */
namespace Util::Surface
{
	/** @brief Per-surface knobs; the defaults come from ThemeManager::Constants. */
	struct Style
	{
		float rounding = -1.0f; /**< @brief Corner radius; negative uses the style's ChildRounding. */
		float alpha = 1.0f;     /**< @brief Multiplier on every layer, for fades. */
		bool shadow = false;    /**< @brief Draws the soft outer shadow (floating surfaces only). */
		bool border = true;     /**< @brief Draws the hairline border. */
		ImU32 borderColor = 0;  /**< @brief Border colour override (e.g. a card's status colour); 0 for the default hairline. */
	};

	/** @brief Fill colour of a surface: the window colour lifted slightly, with the given alpha multiplier. */
	ImU32 FillColor(float alpha = 1.0f);

	/**
	 * @brief Draws a surface into a draw list.
	 * @param drawList Target list (e.g. ImGui::GetWindowDrawList()).
	 * @param min, max Screen rect of the surface.
	 * @param style Look; see Style.
	 */
	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const Style& style = {});
}
