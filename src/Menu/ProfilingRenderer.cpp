#include "ProfilingRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <imgui.h>

#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "State.h"

static ImU32 HslToImU32(float h, float s, float l)
{
	auto hue2rgb = [](float p, float q, float t) -> float {
		if (t < 0.0f)
			t += 1.0f;
		if (t > 1.0f)
			t -= 1.0f;
		if (t < 1.0f / 6.0f)
			return p + (q - p) * 6.0f * t;
		if (t < 0.5f)
			return q;
		if (t < 2.0f / 3.0f)
			return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
		return p;
	};

	float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
	float p = 2.0f * l - q;
	float r = hue2rgb(p, q, h + 1.0f / 3.0f);
	float g = hue2rgb(p, q, h);
	float b = hue2rgb(p, q, h - 1.0f / 3.0f);

	return IM_COL32(
		static_cast<uint8_t>(r * 255.0f),
		static_cast<uint8_t>(g * 255.0f),
		static_cast<uint8_t>(b * 255.0f),
		255);
}

static constexpr float kGoldenRatio = 0.618033988749895f;
static constexpr float kGraphHeadroomScale = 1.2f;
static constexpr float kMainGraphLegendWidth = 260.0f;
static constexpr float kFeatureGraphLegendWidth = 200.0f;
static constexpr float kMinGraphWidth = 100.0f;
static constexpr float kMainGraphHeight = 180.0f;
static constexpr float kFeatureGraphHeight = 100.0f;
static constexpr float kMainGraphMinFrameTimeSec = 0.0001f;
static constexpr float kFeatureGraphMinFrameTimeSec = 0.00001f;
static constexpr float kTimingTableMetricColumnWidth = 55.0f;
static constexpr float kTimingTablePercentColumnWidth = 45.0f;
static constexpr float kStatsRefreshSeconds = 1.0f;

// Comparators for a timing table's columns: Pass by name (case-insensitive), then
// Avg / P95 / P99 by value, and % by the average's share, which orders like the
// average itself. Rows are pointers into the cached entries.
template <class Row, class NameOf, class MetricsOf>
static std::vector<Util::TableRowSortFunc<const Row*>> TimingRowSorts(NameOf a_nameOf, MetricsOf a_metricsOf)
{
	auto byName = [a_nameOf](const Row* a_lhs, const Row* a_rhs, bool a_ascending) {
		auto less = [](const std::string& a_l, const std::string& a_r) {
			return std::ranges::lexicographical_compare(a_l, a_r, [](char a_x, char a_y) {
				return std::tolower(static_cast<unsigned char>(a_x)) < std::tolower(static_cast<unsigned char>(a_y));
			});
		};
		return a_ascending ? less(a_nameOf(*a_lhs), a_nameOf(*a_rhs)) : less(a_nameOf(*a_rhs), a_nameOf(*a_lhs));
	};
	auto byMetric = [a_metricsOf](size_t a_index) {
		return [a_metricsOf, a_index](const Row* a_lhs, const Row* a_rhs, bool a_ascending) {
			const float l = a_metricsOf(*a_lhs)[a_index];
			const float r = a_metricsOf(*a_rhs)[a_index];
			return a_ascending ? l < r : r < l;
		};
	};
	return { byName, byMetric(0), byMetric(1), byMetric(2), byMetric(0) };
}

struct GraphLayout
{
	float graphWidth;
	float legendWidth;
	float height;
	float uiScale;
};

static GraphLayout GetGraphLayout(float availableWidth, float baseLegendWidth, float baseHeight)
{
	const float uiScale = Util::GetUIScale();
	const float contentWidth = std::max(0.0f, availableWidth);
	const float minGraphWidth = kMinGraphWidth * uiScale;
	const float desiredLegendWidth = baseLegendWidth * uiScale;
	const float legendWidth = contentWidth > minGraphWidth ?
	                              std::min(desiredLegendWidth, contentWidth - minGraphWidth) :
	                              0.0f;

	return {
		contentWidth - legendWidth,
		legendWidth,
		baseHeight * uiScale,
		uiScale
	};
}

ImU32 ProfilingRenderer::GetGroupColor(const std::string& groupName)
{
	auto it = groupColorMap.find(groupName);
	if (it != groupColorMap.end())
		return it->second;

	float hue = std::fmod(nextColorIndex * kGoldenRatio, 1.0f);
	ImU32 color = HslToImU32(hue, 0.7f, 0.55f);
	groupColorMap[groupName] = color;
	nextColorIndex++;
	return color;
}

uint32_t ProfilingRenderer::ToLegitColor(ImU32 imColor)
{
	uint8_t r = (imColor >> 0) & 0xFF;
	uint8_t g = (imColor >> 8) & 0xFF;
	uint8_t b = (imColor >> 16) & 0xFF;
	return (0xFF << 24) | (b << 16) | (g << 8) | r;
}

ImVec4 ProfilingRenderer::HeatColor(float value, float maxValue)
{
	if (maxValue <= 0.0f)
		return Util::Colors::GetDefault();

	float x = std::clamp(value / maxValue, 0.0f, 1.0f);

	float x2 = x * x;
	float x3 = x2 * x;
	float x4 = x2 * x2;
	float x5 = x3 * x2;

	float r = 0.13572138f + 4.61539260f * x - 42.66032258f * x2 + 132.13108234f * x3 - 152.94239396f * x4 + 59.28637943f * x5;
	float g = 0.09140261f + 2.19418839f * x + 4.84296658f * x2 - 14.18503333f * x3 + 4.27729857f * x4 + 2.82956604f * x5;
	float b = 0.10667330f + 12.64194608f * x - 60.58204836f * x2 + 110.36276771f * x3 - 89.90310912f * x4 + 27.34824973f * x5;

	float alpha = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).w;

	return ImVec4(std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), alpha);
}

void ProfilingRenderer::TextHeat(const char* fmt, float value, float maxValue)
{
	ImVec4 bg = HeatColor(value, maxValue);
	ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(bg));
	ImGui::TextColored(Util::Colors::GetDefault(), fmt, value);
}

void ProfilingRenderer::RenderTimingModeToggle()
{
	int mode = static_cast<int>(timingMode);

	ImGui::PushID("ProfilingTimingMode");
	ImGui::RadioButton(T("menu.profiling.gpu", "GPU"), &mode, static_cast<int>(TimingMode::GPU));
	ImGui::SameLine();
	ImGui::RadioButton(T("menu.profiling.cpu", "CPU"), &mode, static_cast<int>(TimingMode::CPU));
	ImGui::PopID();

	const auto newMode = static_cast<TimingMode>(mode);
	if (newMode != timingMode) {
		timingMode = newMode;
		timeSinceLastUpdate = kStatsRefreshSeconds;
	}
}

void ProfilingRenderer::SetupTimingTableColumns(bool includePercentColumn)
{
	const float scale = Util::GetUIScale();
	// Timings sort slowest first on the first click, names A to Z.
	constexpr ImGuiTableColumnFlags metricFlags = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending;
	ImGui::TableSetupColumn(T("menu.profiling.pass", "Pass"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
	ImGui::TableSetupColumn(T("menu.profiling.avg", "Avg"), metricFlags, kTimingTableMetricColumnWidth * scale);
	ImGui::TableSetupColumn(T("menu.profiling.p95", "P95"), metricFlags, kTimingTableMetricColumnWidth * scale);
	ImGui::TableSetupColumn(T("menu.profiling.p99", "P99"), metricFlags, kTimingTableMetricColumnWidth * scale);
	if (includePercentColumn)
		ImGui::TableSetupColumn(T("menu.profiling.percent", "%"), metricFlags, kTimingTablePercentColumnWidth * scale);
}

void ProfilingRenderer::RenderGraph()
{
	auto& profiler = (*globals::profiler);
	const auto& results = profiler.GetResults();
	bool cpuMode = (timingMode == TimingMode::CPU);

	if (results.empty())
		return;

	std::vector<legit::ProfilerTask> tasks;

	double accumulated = 0.0;
	for (const auto& result : results) {
		if (!result.valid)
			continue;

		float timeMs = cpuMode ? result.cpuTimeMs : result.gpuTimeMs;

		std::string groupName;
		auto pos = result.name.find("::");
		if (pos != std::string::npos)
			groupName = result.name.substr(0, pos);
		else
			groupName = result.name;

		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + timeMs) / 1000.0;
		task.name = result.name;
		task.color = ToLegitColor(GetGroupColor(groupName));
		tasks.push_back(task);
		accumulated += timeMs;
	}

	if (tasks.empty())
		return;

	gpuGraph.LoadFrameData(tasks.data(), tasks.size());

	float maxFrameTimeSec = gpuGraph.GetPeakFrameTime() * kGraphHeadroomScale;
	if (maxFrameTimeSec < kMainGraphMinFrameTimeSec)
		maxFrameTimeSec = kMainGraphMinFrameTimeSec;

	const auto layout = GetGraphLayout(ImGui::GetContentRegionAvail().x, kMainGraphLegendWidth, kMainGraphHeight);

	gpuGraph.RenderTimings(layout.graphWidth, layout.legendWidth, layout.height, 0, maxFrameTimeSec, layout.uiScale);

	ImGui::Spacing();
}

void ProfilingRenderer::RenderStatistics(bool showTable, bool showModeToggle)
{
	auto& profiler = (*globals::profiler);

	bool cpuMode = (timingMode == TimingMode::CPU);
	if (showModeToggle) {
		RenderTimingModeToggle();
		cpuMode = (timingMode == TimingMode::CPU);
		ImGui::Separator();
	}

	float currentTime = static_cast<float>(ImGui::GetTime());
	float deltaTime = currentTime - lastFrameTime;
	lastFrameTime = currentTime;
	timeSinceLastUpdate += deltaTime;

	if (timeSinceLastUpdate >= kStatsRefreshSeconds) {
		timeSinceLastUpdate = 0.0f;

		cachedGroups.clear();
		cachedTotalAvgMs = 0.0f;
		cachedTotalP95Ms = 0.0f;
		cachedTotalP99Ms = 0.0f;
		cachedMaxAvgMs = 0.0f;
		cachedMaxP95Ms = 0.0f;
		cachedMaxP99Ms = 0.0f;
		std::unordered_map<std::string, size_t> groupIndex;

		for (const auto& result : profiler.GetResults()) {
			if (!result.valid)
				continue;

			float avg = cpuMode ? result.cpuAvgMs : result.avgMs;
			float p95 = cpuMode ? result.cpuP95Ms : result.p95Ms;
			float p99 = cpuMode ? result.cpuP99Ms : result.p99Ms;

			cachedTotalAvgMs += avg;
			cachedTotalP95Ms += p95;
			cachedTotalP99Ms += p99;

			auto pos = result.name.find("::");
			if (pos != std::string::npos) {
				std::string groupName = result.name.substr(0, pos);
				std::string passLabel = result.name.substr(pos + 2);

				auto it = groupIndex.find(groupName);
				if (it == groupIndex.end()) {
					groupIndex[groupName] = cachedGroups.size();
					cachedGroups.push_back({ groupName, 0, 0, 0 });
				}

				auto& group = cachedGroups[groupIndex[groupName]];
				group.totalAvgMs += avg;
				group.totalP95Ms += p95;
				group.totalP99Ms += p99;
				group.passes.push_back({ passLabel, avg, p95, p99 });
			} else {
				groupIndex[result.name] = cachedGroups.size();
				cachedGroups.push_back({ result.name, avg, p95, p99 });
			}
		}

		for (const auto& group : cachedGroups) {
			cachedMaxAvgMs = std::max(cachedMaxAvgMs, group.totalAvgMs);
			cachedMaxP95Ms = std::max(cachedMaxP95Ms, group.totalP95Ms);
			cachedMaxP99Ms = std::max(cachedMaxP99Ms, group.totalP99Ms);
		}
	}

	if (cachedGroups.empty()) {
		ImGui::TextDisabled("%s", T("menu.profiling.no_timing_data_world", "No timing data available (enter game world)"));
		return;
	}

	RenderGraph();

	if (showTable) {
		float availHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();

		if (ImGui::BeginTable("##Profiler", 5,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY |
					ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate,
				ImVec2(0.0f, availHeight))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			SetupTimingTableColumns(true);
			ImGui::TableHeadersRow();

			// Groups sort among themselves and each group's passes within it. No
			// active sort (the tristate's third click) keeps the order the passes ran in.
			const Util::TableSortSpec sort = Util::ReadTableSortSpec();
			const auto groupSorts = TimingRowSorts<GroupEntry>([](const GroupEntry& a_group) -> const std::string& { return a_group.name; },
				[](const GroupEntry& a_group) { return std::array{ a_group.totalAvgMs, a_group.totalP95Ms, a_group.totalP99Ms }; });
			const auto passSorts = TimingRowSorts<PassEntry>([](const PassEntry& a_pass) -> const std::string& { return a_pass.label; },
				[](const PassEntry& a_pass) { return std::array{ a_pass.avgMs, a_pass.p95Ms, a_pass.p99Ms }; });
			std::vector<const GroupEntry*> groups;
			groups.reserve(cachedGroups.size());
			for (const auto& group : cachedGroups)
				groups.push_back(&group);
			Util::SortTableRows(groups, sort, groupSorts);

			for (const GroupEntry* groupRow : groups) {
				const auto& group = *groupRow;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				if (group.passes.empty()) {
					ImGui::TreeNodeEx(group.name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalAvgMs, cachedMaxAvgMs);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP95Ms, cachedMaxP95Ms);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP99Ms, cachedMaxP99Ms);
					ImGui::TableNextColumn();
					if (cachedTotalAvgMs > 0.0f)
						TextHeat("%5.1f", (group.totalAvgMs / cachedTotalAvgMs) * 100.0f, 100.0f);
				} else {
					bool open = ImGui::TreeNodeEx(group.name.c_str(), 0);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalAvgMs, cachedMaxAvgMs);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP95Ms, cachedMaxP95Ms);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP99Ms, cachedMaxP99Ms);
					ImGui::TableNextColumn();
					if (cachedTotalAvgMs > 0.0f)
						TextHeat("%5.1f", (group.totalAvgMs / cachedTotalAvgMs) * 100.0f, 100.0f);
					if (open) {
						std::vector<const PassEntry*> passes;
						passes.reserve(group.passes.size());
						for (const auto& pass : group.passes)
							passes.push_back(&pass);
						Util::SortTableRows(passes, sort, passSorts);
						for (const PassEntry* passRow : passes) {
							const auto& pass = *passRow;
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							ImGui::TreeNodeEx(pass.label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
							ImGui::TableNextColumn();
							TextHeat("%.3f", pass.avgMs, cachedMaxAvgMs);
							ImGui::TableNextColumn();
							TextHeat("%.3f", pass.p95Ms, cachedMaxP95Ms);
							ImGui::TableNextColumn();
							TextHeat("%.3f", pass.p99Ms, cachedMaxP99Ms);
							ImGui::TableNextColumn();
							if (cachedTotalAvgMs > 0.0f)
								TextHeat("%5.1f", (pass.avgMs / cachedTotalAvgMs) * 100.0f, 100.0f);
						}
						ImGui::TreePop();
					}
				}
			}
			ImGui::EndTable();
		}
	}

}

void ProfilingRenderer::RenderFeatureTimers(const std::string& featurePrefix)
{
	auto& profiler = (*globals::profiler);
	const auto& results = profiler.GetResults();

	RenderTimingModeToggle();

	bool cpuMode = (timingMode == TimingMode::CPU);

	struct Entry
	{
		std::string label;
		float timeMs;
		float avgMs;
		float p95Ms;
		float p99Ms;
	};

	std::vector<Entry> entries;
	float totalTimeMs = 0.0f;
	float totalAvg = 0.0f;
	float totalP95 = 0.0f;
	float totalP99 = 0.0f;
	float maxAvg = 0.0f;
	float maxP95 = 0.0f;
	float maxP99 = 0.0f;

	const auto prefix = GetFeatureTimerPrefix(featurePrefix);
	for (const auto& r : results) {
		if (!IsFeatureTimerResult(r, prefix))
			continue;
		std::string label = r.name.substr(prefix.size());
		float timeMs = cpuMode ? r.cpuTimeMs : r.gpuTimeMs;
		float avg = cpuMode ? r.cpuAvgMs : r.avgMs;
		float p95 = cpuMode ? r.cpuP95Ms : r.p95Ms;
		float p99 = cpuMode ? r.cpuP99Ms : r.p99Ms;
		entries.push_back({ label, timeMs, avg, p95, p99 });
		totalTimeMs += timeMs;
		totalAvg += avg;
		totalP95 += p95;
		totalP99 += p99;
		maxAvg = std::max(maxAvg, avg);
		maxP95 = std::max(maxP95, p95);
		maxP99 = std::max(maxP99, p99);
	}

	if (entries.empty()) {
		ImGui::TextDisabled("%s", T("menu.profiling.no_timing_data", "No timing data"));
		return;
	}

	auto& state = featureGraphs[featurePrefix];

	std::vector<legit::ProfilerTask> tasks;
	double accumulated = 0.0;
	for (const auto& e : entries) {
		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + e.timeMs) / 1000.0;
		task.name = e.label;
		task.color = ToLegitColor(GetGroupColor(featurePrefix + "::" + e.label));
		tasks.push_back(task);
		accumulated += e.timeMs;
	}

	if (!tasks.empty()) {
		state.graph.LoadFrameData(tasks.data(), tasks.size());

		float maxFrameTimeSec = state.graph.GetPeakFrameTime() * kGraphHeadroomScale;
		if (maxFrameTimeSec < kFeatureGraphMinFrameTimeSec)
			maxFrameTimeSec = kFeatureGraphMinFrameTimeSec;

		const auto layout = GetGraphLayout(ImGui::GetContentRegionAvail().x, kFeatureGraphLegendWidth, kFeatureGraphHeight);

		state.graph.RenderTimings(layout.graphWidth, layout.legendWidth, layout.height, 0, maxFrameTimeSec, layout.uiScale);
		ImGui::Spacing();
	}

	if (ImGui::BeginTable("##FeatureTimers", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
		SetupTimingTableColumns(false);
		ImGui::TableHeadersRow();

		// The Total row stays last whatever the order.
		std::vector<const Entry*> rows;
		rows.reserve(entries.size());
		for (const auto& e : entries)
			rows.push_back(&e);
		Util::SortTableRows(rows, Util::ReadTableSortSpec(),
			TimingRowSorts<Entry>([](const Entry& a_entry) -> const std::string& { return a_entry.label; },
				[](const Entry& a_entry) { return std::array{ a_entry.avgMs, a_entry.p95Ms, a_entry.p99Ms }; }));

		for (const Entry* row : rows) {
			const auto& e = *row;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%s", e.label.c_str());
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.avgMs, maxAvg);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p95Ms, maxP95);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p99Ms, maxP99);
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		const auto totalColor = globals::menu->GetTheme().StatusPalette.InfoColor;
		ImGui::TextColored(totalColor, "%s", T("menu.profiling.total", "Total"));
		ImGui::TableNextColumn();
		ImGui::TextColored(totalColor, "%.3f", totalAvg);
		ImGui::TableNextColumn();
		ImGui::TextColored(totalColor, "%.3f", totalP95);
		ImGui::TableNextColumn();
		ImGui::TextColored(totalColor, "%.3f", totalP99);

		ImGui::EndTable();
	}
}

bool ProfilingRenderer::HasFeatureTimers(const std::string& featurePrefix)
{
	const auto prefix = GetFeatureTimerPrefix(featurePrefix);
	const auto& results = globals::profiler->GetResults();

	return std::ranges::any_of(results, [&prefix](const auto& result) {
		return IsFeatureTimerResult(result, prefix);
	});
}

std::string ProfilingRenderer::GetFeatureTimerPrefix(const std::string& featurePrefix)
{
	return featurePrefix + "::";
}

bool ProfilingRenderer::IsFeatureTimerResult(const Profiler::TimerResult& result, std::string_view prefix)
{
	return result.valid && result.name.starts_with(prefix);
}
