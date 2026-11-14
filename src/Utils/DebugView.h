#pragma once

#include <initializer_list>
#include <optional>
#include <string>

namespace Util
{
	/** @brief One debug readout: a dim label on the left, the value right-aligned, and an optional colour swatch before it. */
	struct DebugValueRow
	{
		const char* label;                           /**< @brief Already-translated label. */
		std::string value;                           /**< @brief Formatted value. */
		std::optional<ImVec4> swatch = std::nullopt; /**< @brief Colour shown as a small swatch before the value, if any. */
	};

	/**
	 * @brief Draws readouts as a two-column label/value list (use inside a Util::SectionCard).
	 * @param id Stable table id.
	 * @param rows The rows to draw, top to bottom.
	 */
	void DrawDebugValueRows(const char* id, std::initializer_list<DebugValueRow> rows);

	/** @brief Formats a float with three decimals, the precision debug readouts use. */
	std::string DebugNum(float value);
}
