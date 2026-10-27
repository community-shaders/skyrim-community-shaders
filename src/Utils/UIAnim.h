#pragma once

#include <imgui.h>
#include <limits>

/**
 * @brief Small UI animation toolkit: per-ID animated floats kept in ImGui's state storage, easing curves, a
 * hover / press / selected blend for items and colour helpers. Every function honours the "Reduce UI animations"
 * setting by snapping to the target. Values are stored in the current window's state storage, so call
 * the same helper with the same ID from the same window each frame.
 */
namespace Util::Anim
{
	/** @brief True when the user asked for no UI motion (Settings > Behavior > Reduce UI animations). */
	bool ReducedMotion();

	/** @brief Cubic ease-out of t in [0, 1]: quick start, soft landing. */
	float EaseOutCubic(float t);
	/** @brief Smooth ease-in-out (smoothstep) of t in [0, 1]. */
	float EaseInOut(float t);

	/**
	 * @brief One frame-rate independent smoothing step of a value you keep yourself: `current` moves toward `target`
	 * by 1 - exp(-rate * dt), and snaps once within the animation epsilon (or at once under reduced motion).
	 */
	float Smooth(float current, float target, float rate);

	/**
	 * @brief Moves the stored value for `id` toward `target` with frame-rate independent exponential smoothing.
	 * @param id Key in the current window's state storage; build it with ImGui::GetID.
	 * @param target Value to approach.
	 * @param rate Approach speed per second; higher is snappier.
	 * @param initial Value used the first time the id is seen; NaN starts at the target (no animation on first sight).
	 * @return The new value.
	 */
	float Approach(ImGuiID id, float target, float rate, float initial = std::numeric_limits<float>::quiet_NaN());

	/** @brief Approach() toward 1 or 0 using the standard hover rate. */
	float Toggle(ImGuiID id, bool on, float rate = 0.0f);

	/** @brief Animated hover / press / selected state of one item, each in [0, 1]. */
	struct Blend
	{
		float hover = 0.0f;
		float press = 0.0f;
		float selected = 0.0f;
		/** @brief Strongest of the three, for a single highlight alpha. */
		float Highlight() const { return hover > press ? (hover > selected ? hover : selected) : (press > selected ? press : selected); }
	};

	/**
	 * @brief Animated hover / press / selected blend for an item.
	 * @param id Item id (e.g. the one the widget was created with).
	 */
	Blend ItemBlend(ImGuiID id, bool hovered, bool held, bool selected);

	/**
	 * @brief Seconds-based fade / slide-in for something that appears: 0 until `delay` has passed since the item
	 * (re)appeared, then eased up to 1 over `duration`. "Appeared" means it was not asked for in the previous frame,
	 * so it restarts every visit.
	 * @param key Stable id of the page or card.
	 * @param delay Seconds to wait before starting (stagger).
	 * @param duration Seconds to run; defaults to the page-enter time.
	 */
	float EnterProgress(ImGuiID key, float delay = 0.0f, float duration = 0.0f);

	/** @brief Slow 0..1 sine pulse for drawing attention; stays 0 under reduced motion. */
	float Pulse(float periodSeconds = 0.0f);

	/** @brief Linear blend of two colours. */
	ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t);
	/** @brief Linear blend of two packed colours. */
	ImU32 LerpU32(ImU32 a, ImU32 b, float t);
	/** @brief `color` with its alpha multiplied by `alpha`. */
	ImVec4 ScaleAlpha(const ImVec4& color, float alpha);
	/** @brief `color` with its alpha multiplied by `alpha`, packed. */
	ImU32 ScaleAlphaU32(ImU32 color, float alpha);

	/**
	 * @brief Pushes ImGuiStyleVar_Alpha for a popup fading in, restarting each time `key` appears.
	 * @return The eased progress, for scaling a panel as well.
	 */
	float BeginFadeIn(ImGuiID key);
	/** @brief Pops what BeginFadeIn pushed. */
	void EndFadeIn();
}
