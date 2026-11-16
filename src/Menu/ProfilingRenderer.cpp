#include "ProfilingRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>
#include <imgui.h>
#include <unordered_map>
#include <utility>

#include "CSEditor/Browser/BrowserWidgets.h"
#include "Feature.h"
#include "Features/PerformanceOverlay.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuIcons.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Utils/Format.h"
#include "Utils/UI.h"

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
static constexpr float kGroupColorSaturation = 0.7f;  // HSL of the per-group graph colours
static constexpr float kGroupColorLightness = 0.55f;
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
	ImU32 color = HslToImU32(hue, kGroupColorSaturation, kGroupColorLightness);
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
	profiler.KeepActive();
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

		const std::string groupName = GetGroupName(result.name);

		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + timeMs) / 1000.0;
		task.name = result.name;
		task.color = ToLegitColor(GetGroupColor(groupName));
		tasks.push_back(task);
		accumulated += timeMs;
	}

	if (!tasks.empty())
		gpuGraph.Render(tasks, kMainGraphLegendWidth, kMainGraphHeight, kMainGraphMinFrameTimeSec);
}

void ProfilingRenderer::GraphState::Record(const std::vector<legit::ProfilerTask>& tasks)
{
	if (gate.ShouldSample())
		graph.LoadFrameData(tasks.data(), tasks.size());
}

void ProfilingRenderer::GraphState::Render(const std::vector<legit::ProfilerTask>& tasks, float baseLegendWidth, float baseHeight, float minFrameTimeSec)
{
	Record(tasks);
	Draw(baseLegendWidth, baseHeight, minFrameTimeSec);
}

void ProfilingRenderer::GraphState::Draw(float baseLegendWidth, float baseHeight, float minFrameTimeSec)
{
	const float maxFrameTimeSec = std::max(graph.GetPeakFrameTime() * kGraphHeadroomScale, minFrameTimeSec);
	const auto layout = GetGraphLayout(ImGui::GetContentRegionAvail().x, baseLegendWidth, baseHeight);

	graph.RenderTimings(layout.graphWidth, layout.legendWidth, layout.height, 0, maxFrameTimeSec, layout.uiScale);

	ImGui::Spacing();
}

void ProfilingRenderer::SelectTab(Tab tab)
{
	selectedTab = tab;
}

void ProfilingRenderer::RenderPage()
{
	auto* menu = globals::menu;
	const auto& palette = menu->GetTheme().StatusPalette;
	auto& overlay = globals::features::performanceOverlay;
	auto& profiler = *globals::profiler;
	// Timed while the page is open, whichever tool is shown, so the Pass Timings card stays current
	profiler.KeepActive();

	std::array<Util::OptionCardContent, static_cast<size_t>(Tab::Count)> cards{};

	// Pass Timings: GPU time of the last collected frame
	auto& timingsCard = cards[static_cast<size_t>(Tab::PassTimings)];
	timingsCard.icon = ICON_TI_CLOCK;
	timingsCard.title = T("menu.profiling.timings_tab", "Pass Timings");
	timingsCard.description = T("menu.profiling.timings_blurb", "GPU and CPU time of every Community Shaders render pass.");
	std::string timingsStatus;
	if (profiler.GetResults().empty()) {
		timingsCard.status = T("menu.profiling.timings_no_data", "No data yet (enter the game world)");
		timingsCard.statusColor = palette.Disable;
	} else {
		const auto gpuMs = std::format("{:.2f}", profiler.GetTotalTimeMs());
		timingsStatus = std::vformat(T("menu.profiling.timings_gpu_total", "{} ms GPU per frame"), std::make_format_args(gpuMs));
		timingsCard.status = timingsStatus.c_str();
		timingsCard.statusColor = palette.SuccessColor;
	}

	// Performance Overlay: the status line's action turns the overlay window on or off
	auto& overlayCard = cards[static_cast<size_t>(Tab::PerformanceOverlay)];
	overlayCard.icon = ICON_TI_EYE;
	overlayCard.title = T("menu.profiling.overlay_tab", "Performance Overlay");
	overlayCard.description = T("menu.profiling.overlay_blurb", "FPS, frame time graphs, draw calls, VRAM and A/B testing in an in-game window.");
	std::string overlayStatus;
	if (!overlay.loaded) {
		overlayCard.unavailable = true;
		if (globals::state->IsFeatureDisabled(overlay.GetShortName())) {
			overlayCard.status = T("menu.profiling.overlay_disabled_at_boot", "Disabled at boot");
			overlayCard.statusColor = palette.Disable;
		} else if (overlay.installed) {
			overlayCard.status = T("menu.profiling.overlay_restart", "Restart to enable");
			overlayCard.statusColor = palette.RestartNeeded;
		} else {
			overlayCard.status = T("menu.profiling.overlay_not_installed", "Not installed");
			overlayCard.statusColor = palette.Disable;
		}
	} else if (!overlay.settings.ShowInOverlay) {
		overlayCard.status = T("menu.profiling.overlay_off", "Off");
		overlayCard.statusColor = palette.Disable;
		overlayCard.action = T("menu.profiling.overlay_turn_on", "Turn on");
	} else {
		if (menu->overlayVisible) {
			overlayCard.status = T("menu.profiling.overlay_showing", "Showing");
			overlayCard.statusColor = palette.SuccessColor;
		} else {
			const auto hotkey = Util::Input::KeyIdToString(menu->GetSettings().OverlayToggleKey);
			overlayStatus = std::vformat(T("menu.profiling.overlay_hidden", "Hidden: press {} to show"), std::make_format_args(hotkey));
			overlayCard.status = overlayStatus.c_str();
			overlayCard.statusColor = palette.Warning;
		}
		overlayCard.action = T("menu.profiling.overlay_turn_off", "Turn off");
	}

	// Every card takes the tallest one's height so the row lines up
	if (ImGui::BeginTable("##ProfilingToolCards", static_cast<int>(Tab::Count), ImGuiTableFlags_SizingStretchSame)) {
		const float cellWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().CellPadding.x * 2.0f * cards.size()) / cards.size();
		float height = 0.0f;
		for (const auto& card : cards)
			height = std::max(height, Util::OptionCardHeight(card, cellWidth));
		for (size_t i = 0; i < cards.size(); ++i) {
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(i));
			const auto tab = static_cast<Tab>(i);
			const auto click = Util::OptionCard("##ToolCard", cards[i], selectedTab == tab, ImVec2(0.0f, height));
			if (click != Util::OptionCardClick::None)
				selectedTab = tab;
			if (click == Util::OptionCardClick::Action && tab == Tab::PerformanceOverlay) {
				const bool shown = !overlay.settings.ShowInOverlay;
				FeatureListRenderer::RunFeatureCommand(&overlay, [&overlay, shown] { overlay.SetShowInOverlay(shown); });
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	switch (selectedTab) {
	case Tab::PerformanceOverlay:
		FeatureListRenderer::DrawEmbeddedFeaturePage(&overlay);
		break;
	case Tab::PassTimings:
	default:
		RenderStatistics();
		break;
	}
}

void ProfilingRenderer::RenderStatistics(bool showTable, bool showModeToggle)
{
	auto& profiler = (*globals::profiler);
	profiler.KeepActive();

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

		for (const auto& result : profiler.GetStatistics()) {
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

			// Avg / P95 / P99 cells heat-coloured against the column maxima, then the share of the total.
			const auto metricCells = [](float avg, float p95, float p99) {
				ImGui::TableNextColumn();
				TextHeat("%.3f", avg, cachedMaxAvgMs);
				ImGui::TableNextColumn();
				TextHeat("%.3f", p95, cachedMaxP95Ms);
				ImGui::TableNextColumn();
				TextHeat("%.3f", p99, cachedMaxP99Ms);
				ImGui::TableNextColumn();
				if (cachedTotalAvgMs > 0.0f)
					TextHeat("%5.1f", (avg / cachedTotalAvgMs) * 100.0f, 100.0f);
			};

			// Consumed on the first draw, so a group that is not reporting does not grab the view later.
			const std::string focus = std::exchange(focusGroup, {});

			// Right-click a group to pick it in or out of the Performance Overlay's graph.
			// Keyed on the tree node just drawn, so each group gets its own menu.
			const auto groupContextMenu = [](const std::string& a_group) {
				if (ImGui::BeginPopupContextItem()) {
					RenderOverlayGroupToggle(a_group);
					ImGui::EndPopup();
				}
			};

			for (const GroupEntry* groupRow : groups) {
				const auto& group = *groupRow;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// A feature's popover asked for this group: open it and scroll to it.
				if (!focus.empty() && group.name == focus) {
					ImGui::SetScrollHereY(0.25f);
					ImGui::SetNextItemOpen(true);
				}

				if (group.passes.empty()) {
					ImGui::TreeNodeEx(group.name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
					groupContextMenu(group.name);
					metricCells(group.totalAvgMs, group.totalP95Ms, group.totalP99Ms);
					continue;
				}

				const bool open = ImGui::TreeNodeEx(group.name.c_str(), 0);
				groupContextMenu(group.name);
				metricCells(group.totalAvgMs, group.totalP95Ms, group.totalP99Ms);
				if (!open)
					continue;

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
					metricCells(pass.avgMs, pass.p95Ms, pass.p99Ms);
				}
				ImGui::TreePop();
			}
			ImGui::EndTable();
		}
	}
}

ProfilingRenderer::FeatureTimings ProfilingRenderer::CollectFeatureTimings(const std::string& featurePrefix, GraphState& state)
{
	auto& profiler = (*globals::profiler);
	const bool cpuMode = (timingMode == TimingMode::CPU);
	const auto prefix = GetFeatureTimerPrefix(featurePrefix);

	const double currentTime = ImGui::GetTime();
	if (state.lastStatisticsUpdate < 0.0 || currentTime - state.lastStatisticsUpdate >= kStatsRefreshSeconds) {
		state.statistics.clear();
		for (const auto& result : profiler.GetStatistics(prefix))
			state.statistics.insert_or_assign(result.name, PassStatistics{ result.avgMs, result.p95Ms, result.p99Ms, result.cpuAvgMs, result.cpuP95Ms, result.cpuP99Ms });
		state.lastStatisticsUpdate = currentTime;
	}

	FeatureTimings timings;
	for (const auto& r : profiler.GetResults()) {
		if (!IsFeatureTimerResult(r, prefix))
			continue;
		const float timeMs = cpuMode ? r.cpuTimeMs : r.gpuTimeMs;
		// Show a new pass's current sample until the next statistics refresh.
		float avg = timeMs;
		float p95 = timeMs;
		float p99 = timeMs;
		if (const auto cached = state.statistics.find(r.name); cached != state.statistics.end()) {
			const auto& statistics = cached->second;
			avg = cpuMode ? statistics.cpuAvgMs : statistics.avgMs;
			p95 = cpuMode ? statistics.cpuP95Ms : statistics.p95Ms;
			p99 = cpuMode ? statistics.cpuP99Ms : statistics.p99Ms;
		}
		timings.entries.push_back({ r.name.substr(prefix.size()), timeMs, avg, p95, p99 });
		timings.totalAvgMs += avg;
		timings.totalP95Ms += p95;
		timings.totalP99Ms += p99;
		timings.maxAvgMs = std::max(timings.maxAvgMs, avg);
		timings.maxP95Ms = std::max(timings.maxP95Ms, p95);
		timings.maxP99Ms = std::max(timings.maxP99Ms, p99);
	}
	return timings;
}

std::vector<legit::ProfilerTask> ProfilingRenderer::BuildFeatureTasks(const std::string& featurePrefix, const FeatureTimings& timings)
{
	std::vector<legit::ProfilerTask> tasks;
	tasks.reserve(timings.entries.size());
	double accumulated = 0.0;
	for (const auto& e : timings.entries) {
		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + e.timeMs) / 1000.0;
		task.name = e.label;
		task.color = ToLegitColor(GetGroupColor(featurePrefix + "::" + e.label));
		tasks.push_back(task);
		accumulated += e.timeMs;
	}
	return tasks;
}

std::string ProfilingRenderer::FeatureCostChipLabel(const FeatureTimings& timings)
{
	const bool cpuMode = (timingMode == TimingMode::CPU);
	return std::format("{} {}", cpuMode ? T("menu.profiling.cpu", "CPU") : T("menu.profiling.gpu", "GPU"),
		Util::FormatMilliseconds(timings.totalAvgMs));
}

float ProfilingRenderer::MeasureFeatureCostChip(const std::string& featurePrefix)
{
	if (!HasFeatureTimers(featurePrefix))
		return 0.0f;

	const auto timings = CollectFeatureTimings(featurePrefix, featureGraphs[featurePrefix]);
	if (timings.entries.empty())
		return 0.0f;

	MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
	return BrowserUI::MeasureChip(FeatureCostChipLabel(timings).c_str(), true);
}

bool ProfilingRenderer::RenderFeatureCostChip(const std::string& featurePrefix, float rightLimit)
{
	if (!HasFeatureTimers(featurePrefix))
		return false;

	auto& state = featureGraphs[featurePrefix];
	const auto timings = CollectFeatureTimings(featurePrefix, state);
	if (timings.entries.empty())
		return false;
	// Recorded whether or not the popover is open, so it opens on a filled history.
	state.Record(BuildFeatureTasks(featurePrefix, timings));

	const auto& theme = globals::menu->GetTheme();
	const std::string label = FeatureCostChipLabel(timings);
	const ImVec4 costColor = Util::GetThresholdColor(timings.totalAvgMs, ThemeManager::Constants::FEATURE_COST_GOOD_MS,
		ThemeManager::Constants::FEATURE_COST_WARNING_MS, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error);

	const char* popupId = "##FeatureProfilingPopover";
	{
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		if (ImGui::GetCursorScreenPos().x + BrowserUI::MeasureChip(label.c_str(), true) > rightLimit)
			return false;
		const ImVec4 secondary = Util::Colors::GetSecondary();
		ImGui::PushStyleColor(ImGuiCol_Text, secondary);
		if (BrowserUI::Chip("##FeatureCost", label.c_str(), Icons::TI(MenuIcons::kProfiling), &costColor))
			ImGui::OpenPopup(popupId);
		ImGui::PopStyleColor();
	}
	// The popover already shows everything the tooltip would.
	if (!ImGui::IsPopupOpen(popupId)) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("menu.profiling.cost_chip_tooltip", "Average time this feature's passes take each frame. Click for the breakdown."));
			if (ImGui::BeginTable("##FeatureCostTooltip", 2, ImGuiTableFlags_SizingFixedFit)) {
				for (const auto& e : timings.entries) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(e.label.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Util::FormatMilliseconds(e.avgMs).c_str());
				}
				ImGui::EndTable();
			}
		}
	}

	const float popoverWidth = ThemeManager::Constants::FEATURE_PROFILING_POPOVER_WIDTH * Util::GetUIScale();
	ImGui::SetNextWindowSizeConstraints(ImVec2(popoverWidth, 0.0f), ImVec2(popoverWidth, FLT_MAX));
	if (ImGui::BeginPopup(popupId)) {
		{
			MenuFonts::FontRoleGuard body(Menu::FontRole::Body);
			RenderFeatureTimersPopover(featurePrefix, state, timings);
		}
		ImGui::EndPopup();
	}
	return true;
}

void ProfilingRenderer::RenderFeatureTimersPopover(const std::string& featurePrefix, GraphState& state, const FeatureTimings& timings)
{
	Util::SeparatorTextWithFont(GetGroupDisplayName(featurePrefix).c_str(), Menu::FontRole::Subheading);
	RenderTimingModeToggle();

	state.Draw(kFeatureGraphLegendWidth, kFeatureGraphHeight, kFeatureGraphMinFrameTimeSec);

	if (ImGui::BeginTable("##FeatureTimers", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
		SetupTimingTableColumns(false);
		ImGui::TableHeadersRow();

		// The Total row stays last whatever the order.
		std::vector<const FeatureTimerEntry*> rows;
		rows.reserve(timings.entries.size());
		for (const auto& e : timings.entries)
			rows.push_back(&e);
		Util::SortTableRows(rows, Util::ReadTableSortSpec(),
			TimingRowSorts<FeatureTimerEntry>([](const FeatureTimerEntry& a_entry) -> const std::string& { return a_entry.label; },
				[](const FeatureTimerEntry& a_entry) { return std::array{ a_entry.avgMs, a_entry.p95Ms, a_entry.p99Ms }; }));

		for (const FeatureTimerEntry* row : rows) {
			const auto& e = *row;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%s", e.label.c_str());
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.avgMs, timings.maxAvgMs);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p95Ms, timings.maxP95Ms);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p99Ms, timings.maxP99Ms);
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		const auto totalColor = globals::menu->GetTheme().StatusPalette.InfoColor;
		ImGui::TextColored(totalColor, "%s", T("menu.profiling.total", "Total"));
		ImGui::TableNextColumn();
		ImGui::TextColored(totalColor, "%.3f", timings.totalAvgMs);
		ImGui::TableNextColumn();
		ImGui::TextColored(totalColor, "%.3f", timings.totalP95Ms);
		ImGui::TableNextColumn();
		ImGui::TextColored(totalColor, "%.3f", timings.totalP99Ms);

		ImGui::EndTable();
	}

	ImGui::Spacing();
	RenderOverlayGroupToggle(featurePrefix);
	if (globals::features::performanceOverlay.loaded)
		ImGui::SameLine();
	// Right-aligned so it reads as the way out rather than another setting
	const char* openLabel = T("menu.profiling.open_page", "Open Profiling Page");
	const float openWidth = ImGui::CalcTextSize(openLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - openWidth));
	if (Util::Button(openLabel)) {
		OpenProfilingPage(featurePrefix);
		ImGui::CloseCurrentPopup();
	}
	Util::HoverTip(T("menu.profiling.open_page_tooltip", "Every feature's passes side by side, with this feature expanded"));
}

void ProfilingRenderer::RenderOverlayGroupToggle(const std::string& group)
{
	auto& overlay = globals::features::performanceOverlay;
	if (!overlay.loaded)
		return;
	bool shown = overlay.IsProfilerGroupShown(group);
	ImGui::PushID(group.c_str());
	if (ImGui::Checkbox(T("menu.profiling.show_in_overlay", "Show in Performance Overlay"), &shown))
		overlay.SetProfilerGroupShown(group, shown);
	ImGui::PopID();
	Util::HoverTip(T("menu.profiling.show_in_overlay_tooltip", "Include these passes in the Performance Overlay's render pass graph"));
}

void ProfilingRenderer::OpenProfilingPage(const std::string& group)
{
	focusGroup = group;
	selectedTab = Tab::PassTimings;
	FeatureListRenderer::RequestPage(MenuPageIds::kProfiling);
}

void ProfilingRenderer::RenderOverlayGraph()
{
	auto& profiler = (*globals::profiler);
	profiler.KeepActive();
	const auto& overlay = globals::features::performanceOverlay;
	const bool cpuMode = (timingMode == TimingMode::CPU);

	bool anyTimers = false;
	std::vector<legit::ProfilerTask> tasks;
	double accumulated = 0.0;
	for (const auto& result : profiler.GetResults()) {
		if (!result.valid)
			continue;
		anyTimers = true;
		const std::string groupName = GetGroupName(result.name);
		if (!overlay.IsProfilerGroupShown(groupName))
			continue;

		const float timeMs = cpuMode ? result.cpuTimeMs : result.gpuTimeMs;
		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + timeMs) / 1000.0;
		task.name = result.name;
		task.color = ToLegitColor(GetGroupColor(groupName));
		tasks.push_back(task);
		accumulated += timeMs;
	}

	if (!anyTimers) {
		ImGui::TextDisabled("%s", T("menu.profiling.no_timing_data_world", "No timing data available (enter game world)"));
		return;
	}
	if (tasks.empty()) {
		ImGui::TextDisabled("%s", T("menu.profiling.all_groups_hidden", "Every render pass is hidden. Pick some in the Performance Overlay settings."));
		return;
	}
	overlayGraph.Render(tasks, kMainGraphLegendWidth, kMainGraphHeight, kMainGraphMinFrameTimeSec);
}

std::vector<std::string> ProfilingRenderer::GetActiveGroups()
{
	globals::profiler->KeepActive();
	std::vector<std::string> groups;
	for (const auto& result : globals::profiler->GetResults()) {
		if (!result.valid)
			continue;
		auto group = GetGroupName(result.name);
		if (std::ranges::find(groups, group) == groups.end())
			groups.push_back(std::move(group));
	}
	return groups;
}

std::string ProfilingRenderer::GetGroupDisplayName(const std::string& group)
{
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->GetShortName() == group)
			return feature->GetDisplayName();
	}
	return group;
}

std::string ProfilingRenderer::GetGroupName(std::string_view timerName)
{
	const auto pos = timerName.find("::");
	return std::string(pos != std::string_view::npos ? timerName.substr(0, pos) : timerName);
}

bool ProfilingRenderer::HasFeatureTimers(const std::string& featurePrefix)
{
	const auto prefix = GetFeatureTimerPrefix(featurePrefix);
	globals::profiler->KeepActive();
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
