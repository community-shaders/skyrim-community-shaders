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
		if (frameTimeMs <= 0.0f)
			return 0.0f;
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
     * @return Median value. For even-sized vectors, returns the average of the two middle elements.
     */
	inline float Median(std::vector<float> v)
	{
		if (v.empty())
			return 0.0f;

		size_t n = v.size();
		size_t mid = n / 2;

		if (n % 2 == 1) {
			// Odd-sized vector: return the middle element
			std::nth_element(v.begin(), v.begin() + mid, v.end());
			return v[mid];
		} else {
			// Even-sized vector: return average of two middle elements
			std::nth_element(v.begin(), v.begin() + mid - 1, v.end());
			float lower = v[mid - 1];
			std::nth_element(v.begin() + mid, v.begin() + mid, v.end());
			float upper = v[mid];
			return (lower + upper) / 2.0f;
		}
	}

	/**
	 * @brief High-resolution timer using QueryPerformanceCounter
	 * @return Current time in seconds as a double
	 */
	inline double GetNowSecs()
	{
		static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
	}
}