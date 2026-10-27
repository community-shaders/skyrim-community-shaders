#pragma once

#include <imgui.h>

/**
 * @brief Cheap "liquid glass" surfaces: a translucent fill derived from the active theme's window colour, a soft
 * top-edge sheen, a hairline inner border and an optional stack of low-alpha shadow rings. Everything derives from
 * style colours so every theme works, and it all costs a handful of draw-list primitives.
 */
namespace Util::Glass
{
	/** @brief Per-surface knobs; the defaults come from ThemeManager::Constants. */
	struct Style
	{
		float rounding = -1.0f;  /**< @brief Corner radius; negative uses the style's FrameRounding. */
		float alpha = 1.0f;      /**< @brief Multiplier on every layer, for fades. */
		bool shadow = false;     /**< @brief Draws the soft outer shadow. */
		bool sheen = true;       /**< @brief Draws the top-edge highlight gradient and line. */
		ImU32 tint = 0;          /**< @brief Optional colour mixed into the fill (e.g. an accent); 0 for none. */
		float tintAmount = 0.0f; /**< @brief How much of tint replaces the fill colour, 0..1. */
	};

	/** @brief Fill colour of a glass surface (window colour lifted slightly), with the given alpha multiplier. */
	ImU32 FillColor(float alpha = 1.0f);

	/**
	 * @brief Draws a glass surface into a draw list.
	 * @param drawList Target list (e.g. ImGui::GetWindowDrawList() or the background list).
	 * @param min, max Screen rect of the surface.
	 * @param style Look; see Style.
	 */
	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const Style& style = {});

	/** @brief Draws only the soft shadow rings behind a rect, capped at four layers. */
	void DrawShadow(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding, float alpha = 1.0f);
}
