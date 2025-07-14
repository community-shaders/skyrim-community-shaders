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
	void OnABSwitch(ABVariant variant);
	void OnFrame(const std::vector<DrawCallRow>& rows);
	void OnTestEnd();
	std::vector<AggregatedDrawCallStats> GetAggregatedResults() const;
	bool HasResults() const { return !intervals.empty(); }
	void Clear();

	// --- Settings diff functionality ---
	// Call these on first switch to each variant
	void SetSettingsA(const nlohmann::json& settings);
	void SetSettingsB(const nlohmann::json& settings);

	// Test statistics
	float GetTotalTestDuration() const;
	int GetTotalFrameCount() const;
	std::chrono::steady_clock::time_point GetTestStartTime() const { return testStartTime; }
	std::chrono::steady_clock::time_point GetTestEndTime() const { return testEndTime; }
	const std::vector<ABInterval>& GetIntervals() const { return intervals; }

	// Settings access
	const nlohmann::json& GetSettingsA() const { return settingsA; }
	const nlohmann::json& GetSettingsB() const { return settingsB; }
	bool HasSettingsA() const { return hasSettingsA; }
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