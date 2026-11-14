#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Profiler.h"
#include "Utils/Game.h"
#include "Utils/LegitProfiler.h"

/**
 * @brief Renders GPU and CPU profiling statistics, timing graphs, and per-feature timer breakdowns.
 *
 * Provides the Profiling page in the menu with frame time graphs, sortable
 * timing tables grouped by rendering pass, the cost chip in each feature's
 * header with its per-feature popover, and the Performance Overlay's filtered
 * render-pass graph.
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
	 * @brief Draws a feature's cost chip at the cursor: its total average time, which opens a popover with the
	 * graph, the per-pass table and the overlay toggle. Draws nothing when the feature has no timers or the chip
	 * would cross `rightLimit`.
	 *
	 * Call it every frame the feature page is shown: it also records the feature's graph, so the popover opens
	 * on a filled history.
	 *
	 * @param featurePrefix The profiler timer name prefix identifying the feature (e.g. "ScreenSpaceGI").
	 * @param rightLimit Screen x the chip must end before.
	 * @return True when the chip was drawn.
	 */
	static bool RenderFeatureCostChip(const std::string& featurePrefix, float rightLimit);

	/**
	 * @brief Checks whether any profiler timer results exist for the given feature.
	 *
	 * @param featurePrefix The profiler timer name prefix to search for.
	 * @return true if at least one timer result matches the prefix, false otherwise.
	 */
	static bool HasFeatureTimers(const std::string& featurePrefix);

	/**
	 * @brief Renders the Performance Overlay's render-pass graph, leaving out the groups it hides.
	 *
	 * Uses its own graph history, so the Profiling page's unfiltered graph is unaffected.
	 */
	static void RenderOverlayGraph();

	/**
	 * @brief Gets the profiler groups (timer name prefixes before "::") reporting right now, in pass order.
	 */
	static std::vector<std::string> GetActiveGroups();

	/**
	 * @brief Gets the name to show for a profiler group: its feature's display name, or the group itself.
	 * @param group Profiler group, which for a feature is its short name.
	 */
	static std::string GetGroupDisplayName(const std::string& group);

	/**
	 * @brief Draws a checkbox that shows or hides a profiler group in the Performance Overlay's graph.
	 * Draws nothing while the Performance Overlay is not loaded.
	 * @param group Profiler group, which for a feature is its short name.
	 */
	static void RenderOverlayGroupToggle(const std::string& group);

	/**
	 * @brief Opens the Profiling page with a group expanded and scrolled into view.
	 * @param group Profiler group, which for a feature is its short name.
	 */
	static void OpenProfilingPage(const std::string& group);

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

	struct PassStatistics
	{
		float avgMs;
		float p95Ms;
		float p99Ms;
		float cpuAvgMs;
		float cpuP95Ms;
		float cpuP99Ms;
	};

	struct GraphState
	{
		ImGuiUtils::ProfilerGraph graph;
		Util::GraphSampleGate gate;
		double lastStatisticsUpdate = -1.0;
		std::unordered_map<std::string, PassStatistics> statistics;

		// User-provided so the static members below can default-construct it in-class.
		GraphState() :
			graph(Profiler::kHistorySize) {}

		/** @brief Records this frame's tasks, if the gate allows. */
		void Record(const std::vector<legit::ProfilerTask>& tasks);

		/** @brief Draws the recorded history. */
		void Draw(float baseLegendWidth, float baseHeight, float minFrameTimeSec);

		/** @brief Records this frame's tasks (if the gate allows) and draws the graph. */
		void Render(const std::vector<legit::ProfilerTask>& tasks, float baseLegendWidth, float baseHeight, float minFrameTimeSec);
	};
	static inline GraphState gpuGraph;
	static inline GraphState overlayGraph;
	static inline std::unordered_map<std::string, GraphState> featureGraphs;

	/** @brief One of a feature's passes, timed in the current mode. */
	struct FeatureTimerEntry
	{
		std::string label;
		float timeMs;
		float avgMs;
		float p95Ms;
		float p99Ms;
	};

	/** @brief A feature's passes this frame with their totals and column maxima. */
	struct FeatureTimings
	{
		std::vector<FeatureTimerEntry> entries;
		float totalAvgMs = 0.0f;
		float totalP95Ms = 0.0f;
		float totalP99Ms = 0.0f;
		float maxAvgMs = 0.0f;
		float maxP95Ms = 0.0f;
		float maxP99Ms = 0.0f;
	};

	/** @brief Group the Profiling page expands and scrolls to on its next draw; empty when none. */
	static inline std::string focusGroup;

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
	static std::string GetGroupName(std::string_view timerName);
	/** @brief Gathers a feature's timings in the current mode, refreshing its cached statistics once a second. */
	static FeatureTimings CollectFeatureTimings(const std::string& featurePrefix, GraphState& state);
	static std::vector<legit::ProfilerTask> BuildFeatureTasks(const std::string& featurePrefix, const FeatureTimings& timings);
	/** @brief The popover's body: mode toggle, graph, per-pass table, overlay toggle and the Profiling page link. */
	static void RenderFeatureTimersPopover(const std::string& featurePrefix, GraphState& state, const FeatureTimings& timings);
	static bool IsFeatureTimerResult(const Profiler::TimerResult& result, std::string_view prefix);
};
