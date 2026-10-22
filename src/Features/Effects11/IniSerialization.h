#pragma once

#include <format>
#include <span>
#include <string>

namespace Effects11Util::Ini
{
	/** @brief Formats a float with the shortest round-trippable representation. */
	inline std::string FormatFloat(float value)
	{
		return std::format("{}", value);
	}

	/** @brief Formats a span of floats as a comma-separated INI value. */
	inline std::string FormatFloatList(std::span<const float> values)
	{
		std::string result;
		for (size_t i = 0; i < values.size(); ++i) {
			if (i > 0)
				result += ", ";
			result += FormatFloat(values[i]);
		}
		return result;
	}
}
