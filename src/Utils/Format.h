// string and printing related helpers

#pragma once
namespace Util
{
	std::string GetFormattedVersion(const REL::Version& version);

	std::string DefinesToString(const std::vector<std::pair<const char*, const char*>>& defines);
	std::string DefinesToString(const std::vector<D3D_SHADER_MACRO>& defines);

	/**
	 * @brief Normalizes a file path by replacing backslashes with forward slashes,
	 *        removing redundant slashes, and converting all characters to lowercase.
	 *
	 * This function ensures that the file path uses consistent forward slashes
	 * (`/`), eliminates consecutive slashes (`//`), and converts all characters
	 * in the path to lowercase for case-insensitive comparisons.
	 *
	 * @param a_path The original file path to be normalized.
	 * @return A normalized file path as a lowercase string with single forward slashes.
	 */
	std::string FixFilePath(const std::string& a_path);
	std::string WStringToString(const std::wstring& wideString);

	/**
	 * Formats a float value as milliseconds, using 2 or 3 decimal places as appropriate.
	 * Returns '0 ms' for exact zero values.
	 */
	std::string FormatMilliseconds(float ms);
	/**
	 * Formats a float value as microseconds, using 2 decimal places.
	 * Returns '0 us' for exact zero values.
	 */
	std::string FormatMicroseconds(float us);
	/**
	 * Formats a float value as a percentage string with 1 decimal place.
	 */
	std::string FormatPercent(float percent);
	/**
	 * Returns a human-readable string for the time elapsed since the given time point (e.g., '5s', '2m', '1h').
	 */
	std::string TimeAgoString(std::chrono::steady_clock::time_point last);

	/**
	 * Returns a human-readable string for the time elapsed since the given QueryPerformanceCounter time point (e.g., '5s', '2m', '1h').
	 * Uses QueryPerformanceCounter for high-performance timing without std::chrono dependencies.
	 *
	 * @param lastTime LARGE_INTEGER timestamp from QueryPerformanceCounter
	 * @param frequency LARGE_INTEGER frequency from QueryPerformanceFrequency
	 * @return Formatted string showing time elapsed (e.g., "5s", "2m", "1h")
	 */
	std::string TimeAgoStringQPC(const LARGE_INTEGER& lastTime, const LARGE_INTEGER& frequency);

	/**
	 * Formats a delta value with percentage difference for A/B test comparisons.
	 * Returns a string like "+0.45 ms (+12.3%)" or "-0.23 ms (-8.1%)".
	 *
	 * @param a The first value (typically Variant A)
	 * @param b The second value (typically Variant B)
	 * @param threshold Minimum percentage difference to display (default 0.01 = 1%)
	 * @return Formatted string showing delta and percentage difference
	 */
	std::string FormatDeltaWithPercent(float a, float b, float threshold = 0.01f);

	/**
	 * Calculates the percentage of a part relative to a total value.
	 * Returns defaultValue if total is zero or negative.
	 *
	 * @param part The part value to calculate percentage for
	 * @param total The total value to calculate percentage relative to
	 * @param defaultValue Value to return if total is invalid (default 0.0f)
	 * @return Percentage as a float (0.0f to 100.0f)
	 */
	float CalculatePercentage(float part, float total, float defaultValue = 0.0f);

	/**
	 * Calculates the cost per call (frame time per draw call).
	 * Returns 0.0f if drawCalls is zero or negative.
	 *
	 * @param frameTime The frame time in milliseconds
	 * @param drawCalls The number of draw calls
	 * @return Cost per call in milliseconds
	 */
	float CalculateCostPerCall(float frameTime, float drawCalls);

	/**
	 * Calculates the "other" frame time (total frame time minus measured sum).
	 *
	 * @param totalFrameTime The total frame time
	 * @param measuredSum The sum of all measured frame times
	 * @return The remaining frame time not accounted for by measured components
	 */
	float CalculateOtherFrameTime(float totalFrameTime, float measuredSum);
}  // namespace Util