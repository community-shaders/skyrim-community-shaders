#pragma once
#include <cstdint>

namespace Util
{
	/**
     * @brief Converts elapsed timer ticks to milliseconds.
     * @param timeElapsed Number of timer ticks elapsed.
     * @param frequency Timer frequency (ticks per second).
     * @return Elapsed time in milliseconds.
     */
	inline float CalcFrameTime(uint64_t timeElapsed, uint64_t frequency)
	{
		return 1000.0f * static_cast<float>(timeElapsed) / static_cast<float>(frequency);
	}

	/**
     * @brief Calculates frames per second (FPS) from a frame time in milliseconds.
     * @param frameTimeMs Frame time in milliseconds.
     * @return Frames per second.
     */
	inline float CalcFPS(float frameTimeMs)
	{
		return 1000.0f / frameTimeMs;
	}
}