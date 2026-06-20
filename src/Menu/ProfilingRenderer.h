#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Profiler.h"
#include "Utils/LegitProfiler.h"

/**
 * @brief Renders GPU and CPU profiling statistics, timing graphs, and per-feature timer breakdowns.
 *
 * Provides the Profiling page in the menu with frame time graphs, sortable
 * timing tables grouped by rendering pass, and per-feature timer views that
 * can be embedded in individual feature settings panels.
 */
class ProfilingRenderer
{
public:
	/** @brief Selects whether profiling displays GPU or CPU timing data. */
	enum class TimingMode
	{
		GPU,
		CPU
	};

	/**
	 * @brief Renders the main profiling statistics view with timing graph and pass table.
	 *
	 * Draws a frame time graph, a GPU/CPU mode toggle, and a grouped table of
	 * per-pass timing statistics with average, P95, and P99 columns.
	 *
	 * @param showTable If true, renders the detailed timing table below the graph.
	 * @param showModeToggle If true, renders the GPU/CPU timing mode toggle.
	 */
	static void RenderStatistics(bool showTable = true, bool showModeToggle = true);

	/**
	 * @brief Renders timer entries for a specific feature as an inline graph and table.
	 *
	 * Filters profiler results to those matching the given feature prefix and
	 * displays them with a dedicated per-feature timing graph.
	 *
	 * @param featurePrefix The profiler timer name prefix identifying the feature
	 *        (e.g. "ScreenSpaceGI").
	 */
	static void RenderFeatureTimers(const std::string& featurePrefix);

	/**
	 * @brief Checks whether any profiler timer results exist for the given feature.
	 *
	 * @param featurePrefix The profiler timer name prefix to search for.
	 * @return true if at least one timer result matches the prefix, false otherwise.
	 */
	static bool HasFeatureTimers(const std::string& featurePrefix);

private:
	static inline TimingMode timingMode = TimingMode::GPU;
	static inline float timeSinceLastUpdate = 0.0f;
	static inline float lastFrameTime = 0.0f;

	struct PassEntry
	{
		std::string label;
		float avgMs;
		float p95Ms;
		float p99Ms;
	};
	struct GroupEntry
	{
		std::string name;
		float totalAvgMs = 0.0f;
		float totalP95Ms = 0.0f;
		float totalP99Ms = 0.0f;
		std::vector<PassEntry> passes;
	};
	static inline float cachedTotalAvgMs = 0.0f;
	static inline float cachedTotalP95Ms = 0.0f;
	static inline float cachedTotalP99Ms = 0.0f;
	static inline float cachedMaxAvgMs = 0.0f;
	static inline float cachedMaxP95Ms = 0.0f;
	static inline float cachedMaxP99Ms = 0.0f;
	static inline std::vector<GroupEntry> cachedGroups;

	static inline ImGuiUtils::ProfilerGraph gpuGraph{ Profiler::kHistorySize };

	struct FeatureGraphState
	{
		ImGuiUtils::ProfilerGraph graph{ Profiler::kHistorySize };
	};
	static inline std::unordered_map<std::string, FeatureGraphState> featureGraphs;

	static inline std::unordered_map<std::string, ImU32> groupColorMap;
	static inline size_t nextColorIndex = 0;

	static ImU32 GetGroupColor(const std::string& groupName);
	static uint32_t ToLegitColor(ImU32 imColor);
	static ImVec4 HeatColor(float value, float maxValue);
	static void TextHeat(const char* fmt, float value, float maxValue);
	static void RenderTimingModeToggle();
	static void SetupTimingTableColumns(bool includePercentColumn);
	static void RenderGraph();
	static std::string GetFeatureTimerPrefix(const std::string& featurePrefix);
	static bool IsFeatureTimerResult(const Profiler::TimerResult& result, std::string_view prefix);
};
