#pragma once

#include <optional>

/// One typed, clamped location transition duration in seconds. Shared by the widget gutter and the
/// scene page toolbar so the clamp rule lives in exactly one place.
namespace SceneTransitionField
{
	/** @brief Draws a typed duration field, clamped to the range the manager accepts.
	 *  @param a_id Unique within the caller's ImGui id stack.
	 *  @param a_value Override in seconds, cleared to nullopt when the field is emptied.
	 *  @param a_inherited Rendered dimmed while `a_value` is unset.
	 *  @param a_editable False draws the field inert, for a row with no entry to store a duration on.
	 *  @return Whether `a_value` changed this frame. */
	bool Draw(const char* a_id, std::optional<float>& a_value, float a_inherited, bool a_editable);

	/// Width the field occupies, so a caller can budget a toolbar row or reserve an empty slot.
	float GetWidth();
}
