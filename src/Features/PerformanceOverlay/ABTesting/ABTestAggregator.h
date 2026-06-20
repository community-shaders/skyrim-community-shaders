#pragma once
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

// A/B Testing constants
constexpr size_t kFrameHistoryBaseline = 30;
constexpr size_t kMinimumFramesForAnalysis = 10;
constexpr float kOutlierMultiplier = 3.0f;
constexpr float kMaxOutlierFrameTime = 100.0f;

// Statistical validation constants
constexpr int kMinimumSamplesForValidity = 100;      // Industry standard minimum
constexpr float kMinimumTestDuration = 10.0f;        // At least 10 seconds
constexpr float kMinimumValidFramesPercent = 80.0f;  // At least 80% valid frames
constexpr int kMinimumSamplesForMarginal = 30;       // Minimum for marginal validity
constexpr float kMinimumDurationForMarginal = 5.0f;  // Minimum duration for marginal validity

// Only define ABVariant here
enum class ABVariant
{
	A,
	B
};

// Forward declarations
struct DrawCallRow;

struct AggregatedDrawCallStats
{
	std::string label;
	int shaderType;
	float meanA = 0.0f, meanB = 0.0f, delta = 0.0f;
	float medianA = 0.0f, medianB = 0.0f;
	int frameCountA = 0, frameCountB = 0;
	float totalTimeA = 0.0f, totalTimeB = 0.0f;
	// Add more stats as needed
};

struct ABInterval
{
	ABVariant variant;
	std::vector<std::vector<DrawCallRow>> frameRows;
	std::chrono::steady_clock::time_point startTime;
	std::chrono::steady_clock::time_point endTime;
	int excludedFrames = 0;  // Frames excluded due to outliers or shader compilation
};

class ABTestAggregator
{
public:
	/**
	 * @brief Notifies the aggregator of an A/B variant switch.
	 *
	 * Closes the current measurement interval and starts a new one for the given variant.
	 * Records the test start time on the first switch.
	 *
	 * @param variant The variant being switched to.
	 */
	void OnABSwitch(ABVariant variant);

	/**
	 * @brief Records a single frame's draw call data into the current interval.
	 *
	 * Performs outlier detection based on frame time history to exclude
	 * shader compilation spikes and other anomalies from the results.
	 *
	 * @param rows Draw call timing rows captured for this frame.
	 */
	void OnFrame(const std::vector<DrawCallRow>& rows);

	/** @brief Finalises the A/B test by closing the current interval and recording the end time. */
	void OnTestEnd();

	/**
	 * @brief Computes per-shader-type aggregated statistics across all collected intervals.
	 * @return Vector of aggregated stats (mean, median, delta) for each shader type, sorted with summary rows last.
	 */
	std::vector<AggregatedDrawCallStats> GetAggregatedResults() const;

	/** @brief Returns true if at least one measurement interval has been recorded. */
	bool HasResults() const { return !intervals.empty(); }

	/** @brief Resets all intervals, frame history, and captured settings to an empty state. */
	void Clear();

	/**
	 * @brief Captures the settings snapshot for variant A (USER).
	 *
	 * Only stores the snapshot on the first call; subsequent calls are ignored.
	 *
	 * @param settings JSON object containing the full feature settings for variant A.
	 */
	void SetSettingsA(const nlohmann::json& settings);

	/**
	 * @brief Captures the settings snapshot for variant B (TEST).
	 *
	 * Only stores the snapshot on the first call; subsequent calls are ignored.
	 *
	 * @param settings JSON object containing the full feature settings for variant B.
	 */
	void SetSettingsB(const nlohmann::json& settings);

	/**
	 * @brief Returns the total wall-clock duration of the test in seconds.
	 * @return Duration from the first interval start to the last interval end.
	 */
	float GetTotalTestDuration() const;

	/**
	 * @brief Returns the total number of non-outlier frames recorded across all intervals.
	 * @return Accumulated frame count from every interval.
	 */
	int GetTotalFrameCount() const;

	/** @brief Returns the timestamp when the first A/B switch occurred. */
	std::chrono::steady_clock::time_point GetTestStartTime() const { return testStartTime; }

	/** @brief Returns the timestamp when the test ended. */
	std::chrono::steady_clock::time_point GetTestEndTime() const { return testEndTime; }

	/** @brief Returns a read-only reference to all completed measurement intervals. */
	const std::vector<ABInterval>& GetIntervals() const { return intervals; }

	/** @brief Returns the captured settings JSON for variant A (USER). */
	const nlohmann::json& GetSettingsA() const { return settingsA; }

	/** @brief Returns the captured settings JSON for variant B (TEST). */
	const nlohmann::json& GetSettingsB() const { return settingsB; }

	/** @brief Returns true if variant A settings have been captured. */
	bool HasSettingsA() const { return hasSettingsA; }

	/** @brief Returns true if variant B settings have been captured. */
	bool HasSettingsB() const { return hasSettingsB; }

private:
	std::vector<ABInterval> intervals;
	std::unique_ptr<ABInterval> currentInterval;

	// Settings snapshots
	nlohmann::json settingsA;
	nlohmann::json settingsB;
	bool hasSettingsA = false;
	bool hasSettingsB = false;

	// Test timing
	std::chrono::steady_clock::time_point testStartTime;
	std::chrono::steady_clock::time_point testEndTime;

	// Frame history for outlier detection
	std::vector<float> recentFrameTimes;
};