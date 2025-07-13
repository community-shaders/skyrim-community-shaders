#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

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

	/**
     * @brief Calculates the mean of a vector of floats.
     * @param v Vector of floats.
     * @return Mean value.
     */
	inline float Mean(const std::vector<float>& v)
	{
		if (v.empty())
			return 0.0f;
		return std::accumulate(v.begin(), v.end(), 0.0f) / v.size();
	}

	/**
     * @brief Calculates the median of a vector of floats.
     * @param v Vector of floats.
     * @return Median value.
     */
	inline float Median(std::vector<float> v)
	{
		if (v.empty())
			return 0.0f;
		std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
		return v[v.size() / 2];
	}
}