#pragma once

namespace SnowDevSwitches
{
	inline std::atomic<bool> heightMapSplit{ false };

	inline const char* HeightMapRow(const char* a_part)
	{
		return heightMapSplit.load(std::memory_order_relaxed) ? a_part : "SnowDeformation::Objects - Height Maps";
	}
}
