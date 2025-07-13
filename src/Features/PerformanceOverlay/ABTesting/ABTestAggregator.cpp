#include "ABTestAggregator.h"
#include "Features/PerformanceOverlay.h"
#include <algorithm>
#include <map>
#include <numeric>

void ABTestAggregator::OnABSwitch(ABVariant variant)
{
	auto now = std::chrono::steady_clock::now();

	// End the current interval if it exists
	if (currentInterval) {
		currentInterval->endTime = now;
		intervals.push_back(std::move(*currentInterval));
	}

	// Start a new interval
	currentInterval = new ABInterval{ variant, {}, now, now };

	// Record test start time on first switch
	if (intervals.empty()) {
		testStartTime = now;
	}
}

void ABTestAggregator::OnFrame(const std::vector<DrawCallRow>& rows)
{
	if (!currentInterval)
		return;

	// Find the Total row to check for outliers and shader compilation
	float totalFrameTime = 0.0f;
	for (const auto& row : rows) {
		if (row.shaderType == -1) {  // Total row
			totalFrameTime = row.frameTime;
			break;
		}
	}

	// Outlier detection: exclude frames that are more than 3x the median or > 100ms
	// This catches shader compilation spikes, JSON loading, and other anomalies
	static std::vector<float> recentFrameTimes;
	recentFrameTimes.push_back(totalFrameTime);
	if (recentFrameTimes.size() > kFrameHistoryBaseline) {  // Keep last 30 frames for baseline
		recentFrameTimes.erase(recentFrameTimes.begin());
	}

	bool isOutlier = false;
	if (recentFrameTimes.size() >= kMinimumFramesForAnalysis) {  // Need at least 10 frames for statistical analysis
		// Calculate median of recent frames
		std::vector<float> sortedTimes = recentFrameTimes;
		std::sort(sortedTimes.begin(), sortedTimes.end());
		float median = sortedTimes[sortedTimes.size() / 2];

		// Check if current frame is an outlier
		if (totalFrameTime > median * kOutlierMultiplier || totalFrameTime > kMaxOutlierFrameTime) {
			isOutlier = true;
			currentInterval->excludedFrames++;
		}
	}

	// Only add frame if it's not an outlier
	if (!isOutlier) {
		currentInterval->frameRows.push_back(rows);
	}
}

void ABTestAggregator::OnTestEnd()
{
	auto now = std::chrono::steady_clock::now();
	testEndTime = now;

	if (currentInterval) {
		currentInterval->endTime = now;
		intervals.push_back(std::move(*currentInterval));
		currentInterval = nullptr;
	}
}

void ABTestAggregator::Clear()
{
	intervals.clear();
	if (currentInterval) {
		delete currentInterval;
		currentInterval = nullptr;
	}
	hasSettingsA = false;
	hasSettingsB = false;
	settingsA.clear();
	settingsB.clear();
}

static float mean(const std::vector<float>& v)
{
	if (v.empty())
		return 0.0f;
	return std::accumulate(v.begin(), v.end(), 0.0f) / v.size();
}

static float median(std::vector<float> v)
{
	if (v.empty())
		return 0.0f;
	std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
	return v[v.size() / 2];
}

std::vector<AggregatedDrawCallStats> ABTestAggregator::GetAggregatedResults() const
{
	// Map: shaderType -> label
	std::map<int, std::string> labelMap;
	// Map: shaderType -> all frameTimes/costPerCall for A and B
	std::map<int, std::vector<float>> aFrameTimes, bFrameTimes;
	std::map<int, std::vector<float>> aCostPerCall, bCostPerCall;

	for (const auto& interval : intervals) {
		for (const auto& frameRows : interval.frameRows) {
			for (const auto& row : frameRows) {
				labelMap[row.shaderType] = row.label;
				if (interval.variant == ABVariant::A) {
					aFrameTimes[row.shaderType].push_back(row.frameTime);
					aCostPerCall[row.shaderType].push_back(row.costPerCall);
				} else {
					bFrameTimes[row.shaderType].push_back(row.frameTime);
					bCostPerCall[row.shaderType].push_back(row.costPerCall);
				}
			}
		}
	}

	std::vector<AggregatedDrawCallStats> result;
	for (const auto& [shaderType, label] : labelMap) {
		AggregatedDrawCallStats stats;
		stats.label = label;
		stats.shaderType = shaderType;
		stats.meanA = mean(aFrameTimes[shaderType]);
		stats.meanB = mean(bFrameTimes[shaderType]);
		stats.medianA = median(aFrameTimes[shaderType]);
		stats.medianB = median(bFrameTimes[shaderType]);
		stats.delta = stats.meanB - stats.meanA;
		stats.frameCountA = static_cast<int>(aFrameTimes[shaderType].size());
		stats.frameCountB = static_cast<int>(bFrameTimes[shaderType].size());
		stats.totalTimeA = std::accumulate(aFrameTimes[shaderType].begin(), aFrameTimes[shaderType].end(), 0.0f);
		stats.totalTimeB = std::accumulate(bFrameTimes[shaderType].begin(), bFrameTimes[shaderType].end(), 0.0f);
		result.push_back(stats);
	}

	// Sort: put Total (-1) and Other (-2) at the end
	std::sort(result.begin(), result.end(), [](const AggregatedDrawCallStats& a, const AggregatedDrawCallStats& b) {
		// Special handling for summary rows - always put them at the end
		if (a.shaderType == -1 || a.shaderType == -2) {
			if (b.shaderType == -1 || b.shaderType == -2) {
				// Both are summary rows, sort by shader type (Other before Total)
				return a.shaderType < b.shaderType;
			}
			return false;  // a is summary, b is not, so a goes after b
		}
		if (b.shaderType == -1 || b.shaderType == -2) {
			return true;  // b is summary, a is not, so b goes after a
		}
		// Both are regular shaders, sort normally
		return a.shaderType < b.shaderType;
	});

	// Ensure Total and Other are always included even if they have no data
	bool hasTotal = false, hasOther = false;
	for (const auto& stat : result) {
		if (stat.shaderType == -1)
			hasTotal = true;
		if (stat.shaderType == -2)
			hasOther = true;
	}

	if (!hasTotal) {
		AggregatedDrawCallStats totalStat;
		totalStat.label = "Total:";
		totalStat.shaderType = -1;
		result.push_back(totalStat);
	}

	if (!hasOther) {
		AggregatedDrawCallStats otherStat;
		otherStat.label = "Other:";
		otherStat.shaderType = -2;
		result.push_back(otherStat);
	}
	return result;
}

void ABTestAggregator::SetSettingsA(const nlohmann::json& settings)
{
	if (!hasSettingsA) {
		settingsA = settings;
		hasSettingsA = true;
	}
}

void ABTestAggregator::SetSettingsB(const nlohmann::json& settings)
{
	if (!hasSettingsB) {
		settingsB = settings;
		hasSettingsB = true;
	}
}

float ABTestAggregator::GetTotalTestDuration() const
{
	if (intervals.empty())
		return 0.0f;

	auto start = intervals.front().startTime;
	auto end = intervals.back().endTime;
	return std::chrono::duration<float>(end - start).count();
}

int ABTestAggregator::GetTotalFrameCount() const
{
	int total = 0;
	for (const auto& interval : intervals) {
		total += static_cast<int>(interval.frameRows.size());
	}
	return total;
}
