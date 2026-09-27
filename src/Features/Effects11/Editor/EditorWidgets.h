#pragma once

#include <imgui.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

/**
 * @brief Shared building blocks for the Effects 11 editor windows.
 *
 * Both the preset settings panel and the shader parameter panel lay values out as
 * two-column (label | value) rows and pick the same range-aware widgets, so a value
 * behaves identically wherever it is shown.
 */
namespace Effects11UI
{
	/**
	 * @brief Turns an ini key such as "DirectLightingIntensity" into "Direct Lighting Intensity".
	 * Acronyms stay together ("HDRBloom" -> "HDR Bloom") and short joining words are lowercased.
	 */
	std::string PrettifyName(std::string_view a_key);

	/** @brief Case-insensitive substring test; an empty filter matches everything. */
	bool ContainsNoCase(std::string_view a_text, std::string_view a_filter);

	/** @brief Number of decimals that shows a step size without noise (0.01 -> 2, 0.001 -> 3). */
	int DecimalsForStep(float a_step);

	/** @brief Builds a printf format with the given decimals and an optional text prefix ("Day  %.2f"). */
	std::string MakeFormat(int a_decimals, std::string_view a_prefix = {});

	/**
	 * @brief Begins a two-column (label | value) table with striped rows.
	 * @return False when the table is clipped; EndPropertyTable must only be called on true.
	 */
	bool BeginPropertyTable(const char* a_id);
	void EndPropertyTable();

	/** @brief Optional short tag drawn after a label, e.g. "W" for weather-separated values. */
	struct LabelBadge
	{
		const char* text = nullptr;
		ImVec4 color{};
		const char* tooltip = nullptr;
	};

	/**
	 * @brief Starts a row, draws the label cell and moves to the value cell. The next widget is
	 * stretched to the value column width.
	 * @param a_label Text for the label column.
	 * @param a_dimmed Draws the label in the disabled color (read-only or inactive values).
	 * @param a_badge Optional tag drawn after the label.
	 * @return True while the label is hovered (also when disabled), so callers can attach a tooltip.
	 */
	bool PropertyLabel(const char* a_label, bool a_dimmed = false, const LabelBadge& a_badge = {});

	/** @brief Starts a row with an empty label cell, for extra lines that belong to the row above. */
	void PropertyContinuation();

	/**
	 * @brief Range-aware float editor: a slider for small ranges, a drag field for wide ones.
	 * Values are always clamped to [a_min, a_max]. Ctrl+click or double-click types a value.
	 */
	bool FloatValue(const char* a_id, float* a_value, float a_min, float a_max, float a_step = 0.0f, const char* a_format = "%.3f");

	/** @brief FloatValue for 2 to 4 components sharing one range. */
	bool FloatNValue(const char* a_id, float* a_values, int a_components, float a_min, float a_max, const char* a_format = "%.3f");

	/** @brief Range-aware int editor, clamped to [a_min, a_max]. */
	bool IntValue(const char* a_id, int* a_value, int a_min, int a_max);

	/**
	 * @brief Color editor with RGB inputs and a picker. HDR colors may exceed 1.
	 * @param a_components 3 for RGB, 4 for RGBA.
	 */
	bool ColorValue(const char* a_id, float* a_color, int a_components, bool a_hdr);

	/** @brief Draws a small colored tag inline (e.g. "W" for weather-separated values). */
	void Badge(const char* a_text, const ImVec4& a_color, const char* a_tooltip = nullptr);

	/**
	 * @brief Draws short tags right-aligned on the last item's line, e.g. on a collapsing header.
	 * Tags are drawn directly and take no input, so the header stays clickable.
	 */
	void HeaderTags(std::initializer_list<std::pair<const char*, ImVec4>> a_tags, float a_rightInset = 0.0f);

	/** @brief One-slot clipboard so a value can be copied from one setting and pasted into another. */
	namespace Clipboard
	{
		/** @brief Colors are copied as RGB; alpha is left untouched on paste. */
		inline constexpr int kColorComponents = 3;

		void SetFloat(float a_value);
		void SetColor(const float* a_rgb);
		bool HasFloat();
		bool HasColor();
		float GetFloat();
		void GetColor(float* a_rgb);
	}
}
