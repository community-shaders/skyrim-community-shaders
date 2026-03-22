/**
 * @file PerformanceOverlay.cpp
 * @brief Real-time performance monitoring system for Skyrim Community Shaders
 *
 * This module provides comprehensive performance monitoring capabilities including:
 * - Real-time FPS and frame time tracking with configurable update intervals
 * - Interactive draw call analysis with per-shader type performance breakdown
 * - VRAM usage monitoring with visual progress bars
 * - Frame time graphs for pre and post-frame generation analysis
 * - A/B testing support for performance comparison between configurations
 * - Color-coded performance metrics with customizable thresholds
 * - Movable overlay window with persistent positioning
 *
 * The overlay integrates with the A/B testing system to provide live performance
 * comparisons between different shader configurations, helping users optimize
 * their setup for maximum performance while maintaining visual quality.
 *
 */

#include "PerformanceOverlay.h"
#include "Feature.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <numeric>

// --- Constants ---
constexpr float kDefaultFPS = 60.0f;
constexpr float kDefaultFrameTimeMs = 1000.0f / kDefaultFPS;

// --- Helper Structures and Functions ---

// Helper function to create metric columns with consistent formatting
auto MakeMetricColumn(const auto& theme, auto valueGetter, auto colorGetter, auto formatter, const Util::ColoredTextLines& legend, const Util::ColoredTextLines* cellLegend = nullptr)
{
	return [theme, valueGetter, colorGetter, formatter, legend, cellLegend](const DrawCallRow& row, int) {
		using ValueType = decltype(valueGetter(row));
		if constexpr (std::is_same_v<ValueType, std::optional<float>>) {
			if (!valueGetter(row).has_value()) {
				ImGui::TextDisabled("-");
				return;
			}
			float value = *valueGetter(row);
			ImVec4 color = colorGetter(theme, value, row);
			std::string valueStr = formatter(value, row);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", valueStr.c_str());
			ImGui::PopStyleColor();
		} else {
			float value = valueGetter(row);
			ImVec4 color = colorGetter(theme, value, row);
			std::string valueStr = formatter(value, row);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", valueStr.c_str());
			ImGui::PopStyleColor();
		}
		if (ImGui::IsItemHovered()) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				const Util::ColoredTextLines& useLegend = cellLegend ? *cellLegend : legend;
				Util::DrawColoredMultiLineTooltip(useLegend);
			}
		}
	};
}

// --- Helper Functions ---
/**
  * @brief Calculates summary data (Other frame time, percentages, cost per call) from measured sum
  * @param smoothedFrameTime The total smoothed frame time
  * @param measuredSum The sum of measured frame times
  * @return Tuple of (otherFrameTime, otherPercent, totalCostPerCall)
  */
static std::tuple<float, float, float> CalculateSummaryData(float smoothedFrameTime, float measuredSum)
{
	float totalSmoothedDrawCalls = globals::state->GetTotalSmoothedDrawCalls();
	float otherFrameTime = Util::CalculateOtherFrameTime(smoothedFrameTime, measuredSum);
	float otherPercent = Util::CalculatePercentage(otherFrameTime, smoothedFrameTime);
	float totalCostPerCall = Util::CalculateCostPerCall(smoothedFrameTime, totalSmoothedDrawCalls);
	return { otherFrameTime, otherPercent, totalCostPerCall };
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PerformanceOverlay::Settings,
	ShowInOverlay,
	ShowDrawCalls,
	ShowVRAM,
	ShowFPS,
	ShowPreFGFrameTimeGraph,
	ShowPostFGFrameTimeGraph,
	UpdateInterval,
	FrameHistorySize,
	TextSize,
	BackgroundOpacity,
	ShowBorder,
	Position,
	PositionSet)

static const std::unordered_map<RE::BSShader::Type, std::string> kShaderTypeTooltips = {
	{ RE::BSShader::Type::Grass, "Draw calls using the Grass shader. Typically many, but each is usually cheap." },
	{ RE::BSShader::Type::Sky, "Draw calls for the sky dome, clouds, and related effects." },
	{ RE::BSShader::Type::Water, "Draw calls for water surfaces and effects." },
	{ RE::BSShader::Type::Lighting, "Draw calls for dynamic and static lighting passes." },
	{ RE::BSShader::Type::Effect, "Draw calls for special effects, particles, and post-processing." },
	{ RE::BSShader::Type::Utility, "Draw calls for utility passes, such as shadow masks or G-buffer fills." },
	{ RE::BSShader::Type::DistantTree, "Draw calls for distant tree rendering (LOD vegetation)." },
	{ RE::BSShader::Type::Particle, "Draw calls for particle systems (smoke, sparks, etc.)." },
	{ RE::BSShader::Type::BloodSplatter, "Draw calls for blood splatter effects." },
	{ RE::BSShader::Type::ImageSpace, "Draw calls for image space post-processing effects." }
};
// ============================================================================
// VIRTUAL OVERRIDES (Feature.h interface)
// ============================================================================

std::pair<std::string, std::vector<std::string>> PerformanceOverlay::GetFeatureSummary()
{
	std::string description = "Real-time performance monitoring system that displays FPS, frame times, draw calls, VRAM usage, and detailed shader performance analysis.";

	std::vector<std::string> keyFeatures = {
		"Real-time FPS and frame time monitoring with configurable update intervals",
		"Interactive draw call analysis with per-shader type performance breakdown",
		"VRAM usage monitoring with visual progress bars",
		"Frame time graphs for pre and post-frame generation analysis",
		"A/B testing support for performance comparison between configurations",
		"Color-coded performance metrics with customizable thresholds",
		"Movable overlay window with persistent positioning"
	};

	return { description, keyFeatures };
}

void PerformanceOverlay::DrawSettings()
{
	auto menu = Menu::GetSingleton();
	const auto& themeSettings = menu->GetTheme();
	const auto& menuSettings = menu->GetSettings();
	ImGui::Checkbox("Show in Overlay", &this->settings.ShowInOverlay);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Opens performance overlay in a separate window that stays open\neven when the main menu is closed. ");
		ImGui::Text("Toggle with ");
		ImGui::SameLine();
		ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s",
			Util::Input::KeyIdToString(menuSettings.OverlayToggleKey).c_str());
	}

	if (this->settings.ShowInOverlay) {
		ImGui::Indent();

		// Display options
		ImGui::TextUnformatted("Display Options");
		ImGui::Separator();

		ImGui::Checkbox("Show FPS Counter", &this->settings.ShowFPS);
		ImGui::Checkbox("Show Draw Calls", &this->settings.ShowDrawCalls);
		ImGui::Checkbox("Show VRAM Usage", &this->settings.ShowVRAM);

		bool isFrameGenerationActive = globals::features::upscaling.IsFrameGenerationActive();
		if (this->settings.ShowFPS && isFrameGenerationActive) {
			ImGui::Checkbox("Show Pre-FG Frametime Graph", &this->settings.ShowPreFGFrameTimeGraph);

			ImGui::Checkbox("Show Post-FG Frametime Graph", &this->settings.ShowPostFGFrameTimeGraph);
			if (ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("FSR Frame Generation uses calculated timing data (2x Pre-FG).\nDLSS Frame Generation provides measured timing data.");
				}
			}
		} else if (this->settings.ShowFPS) {
			ImGui::Checkbox("Show Frametime Graph", &this->settings.ShowPreFGFrameTimeGraph);
		}

		ImGui::Spacing();
		ImGui::Spacing();

		// Appearance settings
		ImGui::TextUnformatted("Appearance");
		ImGui::Separator();

		ImGui::SliderFloat("Text Size", &this->settings.TextSize, 0.8f, 1.2f, "%.2f");
		ImGui::SliderFloat("Background Opacity", &this->settings.BackgroundOpacity, 0.0f, 1.0f, "%.2f");
		ImGui::Checkbox("Show Border", &this->settings.ShowBorder);
		ImGui::SliderFloat("Update Interval", &this->settings.UpdateInterval, 0.001f, PerformanceOverlay::Settings::kMaxUpdateInterval, "%.2f seconds");
		ImGui::SliderInt("Frame History Size", &this->settings.FrameHistorySize,
			this->settings.kMinFrameHistorySize, this->settings.kMaxFrameHistorySize);

		ImGui::Separator();
		ImGui::Text("Position:");
		if (ImGui::Button("Reset Position")) {
			this->settings.PositionSet = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Restore Defaults")) {
			RestoreDefaultSettings();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Restores Performance Overlay settings to defaults, including graphs, appearance, and update intervals.");
		}

		ImGui::Unindent();
	}
}

void PerformanceOverlay::SaveSettings(json& j)
{
	// Persist all overlay settings to JSON
	j = this->settings;  // uses NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT
}

void PerformanceOverlay::LoadSettings(json& j)
{
	try {
		// Load all settings from JSON (missing fields use defaults)
		this->settings = j.get<PerformanceOverlay::Settings>();
	} catch (...) {
		// Fallback to defaults if JSON is invalid
		this->settings = PerformanceOverlay::Settings{};
	}
	// Ensure history buffers match loaded size
	this->state.frameTimeHistory.Resize(this->settings.FrameHistorySize);
	this->state.postFGFrameTimeHistory.Resize(this->settings.FrameHistorySize);
}

void PerformanceOverlay::RestoreDefaultSettings()
{
	this->settings = PerformanceOverlay::Settings{};
	// Reset runtime buffers/state to match defaults
	this->state.frameTimeHistory.Resize(this->settings.FrameHistorySize);
	this->state.postFGFrameTimeHistory.Resize(this->settings.FrameHistorySize);
	this->state.smoothFps = 0.0f;
	this->state.smoothFrameTimeMs = 0.0f;
	this->state.postFGSmoothFps = 0.0f;
	this->state.postFGSmoothFrameTimeMs = 0.0f;
	this->state.minFrameTime = 1000.0f;
	this->state.maxFrameTime = 0.0f;
	this->state.smoothedMinFrameTime = 0.0f;
	this->state.smoothedMaxFrameTime = 50.0f;
}

void PerformanceOverlay::DataLoaded()
{
	// Initialize performance overlay state
	REX::W32::QueryPerformanceFrequency(&this->state.frequency);
	REX::W32::QueryPerformanceCounter(&this->state.lastFrameCounter);
	this->state.frameTimeHistory.Resize(this->settings.FrameHistorySize);
	this->state.postFGFrameTimeHistory.Resize(this->settings.FrameHistorySize);
}

void PerformanceOverlay::DrawOverlay()
{
	auto* menu = Menu::GetSingleton();

	if (!globals::state || !menu) {
		return;
	}
	if (!menu->overlayVisible) {
		return;
	}
	if (this->settings.ShowVRAM && (!menu->GetDXGIAdapter3())) {
		return;
	}
	if (!ImGui::GetCurrentContext()) {
		return;
	}
	if (!this->settings.ShowInOverlay) {
		return;
	}

	// Build draw call rows ONCE per frame and reuse
	auto [mainRows, summaryRows] = this->BuildDrawCallRows();
	std::vector<DrawCallRow> allRows = mainRows;
	allRows.insert(allRows.end(), summaryRows.begin(), summaryRows.end());

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!menu->IsEnabled) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	if (!this->settings.ShowBorder) {
		windowFlags |= ImGuiWindowFlags_NoBackground;
	} else {
		windowFlags &= ~ImGuiWindowFlags_NoDecoration;
		windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
	}

	// Set background opacity
	ImGui::PushStyleColor(ImGuiCol_WindowBg,
		ImVec4(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x,
			ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).y,
			ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).z,
			this->settings.BackgroundOpacity));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, this->settings.ShowBorder ? ImGui::GetStyle().WindowBorderSize : 0.0f);

	const float scale = Util::GetUIScale();

	// Set initial position if not already set
	if (!this->settings.PositionSet) {
		const float defaultPad = Settings::kDefaultWindowPadding * scale;
		ImGui::SetNextWindowPos(ImVec2(defaultPad, defaultPad));
		this->settings.Position = ImVec2(defaultPad, defaultPad);
		this->settings.PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(this->settings.Position, ImGuiCond_FirstUseEver);
	}

	// Set window size based on whether graphs are shown, was rapidly changing size based on text
	bool hasGraphs = this->settings.ShowPreFGFrameTimeGraph ||
	                 (this->settings.ShowPostFGFrameTimeGraph && this->state.isFrameGenerationActive);
	if (!hasGraphs) {
		// Calculate minimum width needed based on actual content
		float minWidth = 0.0f;

		// Calculate width needed for each enabled section
		if (this->settings.ShowFPS) {
			// Measure FPS text width
			std::string fpsText = std::format("{:.1f} ({:.2f} ms)", this->state.smoothFps, this->state.smoothFrameTimeMs);
			if (this->state.isFrameGenerationActive) {
				fpsText = std::format("Raw FPS: {:.1f} ({:.2f} ms)", this->state.smoothFps, this->state.smoothFrameTimeMs);
			}
			float fpsWidth = ImGui::CalcTextSize(fpsText.c_str()).x;
			minWidth = std::max(minWidth, fpsWidth + Settings::kLabelPadding * scale);
		}
		if (this->settings.ShowDrawCalls) {
			minWidth = std::max(minWidth, Settings::kDrawCallsTableWidth * scale * this->settings.TextSize);
		}
		if (this->settings.ShowVRAM && menu->GetDXGIAdapter3()) {
			minWidth = std::max(minWidth, Settings::kVRAMSectionWidth * scale * this->settings.TextSize);
		}

		minWidth += Settings::kWindowBorderPadding * scale;

		// Set minimum width, but allow auto-resize for larger content
		ImGui::SetNextWindowSize(ImVec2(minWidth, 0), ImGuiCond_FirstUseEver);
	}

	// Create the window
	ImGui::Begin("Performance Overlay", NULL, windowFlags);

	// Remember window position for next frame
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetWindowPos(this->settings.Position);
	}

	// Track if window has been moved
	ImVec2 currentPos = ImGui::GetWindowPos();
	if (currentPos.x != this->settings.Position.x || currentPos.y != this->settings.Position.y) {
		this->settings.Position = currentPos;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scale, 1.0f * scale));
	ImGui::SetWindowFontScale(this->settings.TextSize);

	// Update graph values
	this->UpdateGraphValues();

	// Show FPS counter if enabled
	if (this->settings.ShowFPS) {
		DrawFPS();
	}

	// Show Draw Calls if enabled
	if (this->settings.ShowDrawCalls) {
		DrawDrawCallsTable(mainRows, summaryRows);
	}

	// VRAM & GPU Usage
	if (this->settings.ShowVRAM && menu->GetDXGIAdapter3()) {
		DrawVRAM();
	}

	ImGui::PopStyleVar();             // ItemSpacing
	ImGui::SetWindowFontScale(1.0f);  // Reset font scale

	// --- A/B Test Section ---
	DrawABTestSection(allRows);

	ImGui::End();
	ImGui::PopStyleVar();    // WindowBorderSize
	ImGui::PopStyleColor();  // WindowBg
}
// ============================================================================
// CORE PERFORMANCE DISPLAY FUNCTIONS
// ============================================================================

void PerformanceOverlay::DrawFPS()
{
	if (ImGui::BeginTable("FrametimeTargets", 2, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("##prop", ImGuiTableColumnFlags_WidthFixed, ImGui::GetTextLineHeight() * 5);
		ImGui::TableSetupColumn("##value");

		ImGui::TableNextColumn();
		ImGui::Text(this->state.isFrameGenerationActive ? "Raw FPS:" : "FPS:");
		ImGui::TableNextColumn();

		// Check if buffer is full for the avg
		auto frameData = this->state.frameTimeHistory.GetData();
		size_t validFrameCount = std::count_if(frameData.begin(), frameData.end(), [](float ft) { return ft > 0.0f; });
		bool bufferIsFull = validFrameCount == frameData.size();

		if (bufferIsFull) {
			float avgFrameTime = std::accumulate(frameData.begin(), frameData.end(), 0.0f) / frameData.size();
			float avgFps = (avgFrameTime > 0.001f) ? 1000.0f / avgFrameTime : 0.0f;
			ImGui::Text("%.1f (%.2f ms) | Avg: %.1f", this->state.smoothFps, this->state.smoothFrameTimeMs, avgFps);
		} else {
			ImGui::Text("%.1f (%.2f ms)", this->state.smoothFps, this->state.smoothFrameTimeMs);
		}

		if (this->state.isFrameGenerationActive) {
			ImGui::TableNextColumn();
			ImGui::Text("Post-FG FPS:");
			ImGui::TableNextColumn();
			ImGui::Text("%.1f (%.2f ms)", this->state.postFGSmoothFps, this->state.postFGSmoothFrameTimeMs);
		}

		ImGui::EndTable();
	}

	// Show Pre-FG frametime graph if enabled
	if (this->settings.ShowPreFGFrameTimeGraph) {
		// Prepare overlay text
		char overlay_text[128];
		snprintf(overlay_text, IM_ARRAYSIZE(overlay_text),
			"%s%.2f ms (%.1f FPS)",
			this->state.isFrameGenerationActive ? "Pre-FG: " : "",
			this->state.smoothFrameTimeMs, this->state.smoothFps);

		// Set graph colors
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));  // Green line

		// Draw the graph
		float graphWidth = ImGui::GetWindowWidth() * 0.9f;
		ImGui::PlotLines("##frametime",
			this->state.frameTimeHistory.GetData().data(),
			this->settings.FrameHistorySize,
			static_cast<int>(this->state.frameTimeHistory.GetHeadIdx()),
			overlay_text,
			this->state.smoothedMinFrameTime, this->state.smoothedMaxFrameTime,
			ImVec2(graphWidth, 50.0f * Util::GetUIScale() * this->settings.TextSize));

		ImGui::PopStyleColor();

		// Draw frametime target reference lines
		if (ImGui::BeginTable("FrametimeTargets", 3, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			ImGui::Text("30 FPS: 33.3 ms");

			ImGui::TableNextColumn();
			ImGui::Text("60 FPS: 16.7 ms");

			ImGui::TableNextColumn();
			ImGui::Text("120 FPS: 8.3 ms");

			ImGui::EndTable();
		}
	}

	// Show Post-FG frametime graph if enabled
	if (this->settings.ShowPostFGFrameTimeGraph && this->state.isFrameGenerationActive) {
		// Check if FSR frame generation is active (FSR doesn't provide timing data)
		bool isFrameGenActive = globals::features::upscaling.IsFrameGenerationActive();

		if (isFrameGenActive) {
			// Show note that FSR uses calculated data
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Post-FG: Calculated timing (2x Pre-FG)");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("AMD FSR Frame Generation uses calculated timing data (2x Pre-FG).\nNVIDIA DLSS Frame Generation provides measured timing data.");
			}
		}

		// Show post-FG graph for both DLSS and FSR (FSR uses calculated data)
		this->DrawPostFGFrameTimeGraph();
	}
}

void PerformanceOverlay::DrawVRAM()
{
	auto menu = Menu::GetSingleton();
	if (!menu)
		return;
	auto dxgiAdapter3 = menu->GetDXGIAdapter3();
	if (!dxgiAdapter3)
		return;
	DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo{};
	HRESULT hr = dxgiAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo);

	// Only proceed if the call succeeded and Budget is not zero
	if (SUCCEEDED(hr) && videoMemoryInfo.Budget > 0) {
		float currentGpuUsage = videoMemoryInfo.CurrentUsage / (1024.f * 1024.f * 1024.f);
		float totalGpuMemory = videoMemoryInfo.Budget / (1024.f * 1024.f * 1024.f);
		float percent = currentGpuUsage / totalGpuMemory;

		// Center the VRAM text
		ImGui::Text("VRAM Usage:");

		// Use a centered text format for the numeric values
		std::string vramText = std::format("{:.2f}GB/{:.2f}GB ({:.1f}%)", currentGpuUsage, totalGpuMemory, 100 * percent);
		float textWidth = ImGui::CalcTextSize(vramText.c_str()).x;
		float windowWidth = ImGui::GetWindowWidth();

		// Center the text if it fits within the window
		if (textWidth < windowWidth) {
			ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
			ImGui::Text("%s", vramText.c_str());
		} else {
			ImGui::Text("%s", vramText.c_str());
		}

		// Only move the progress bar, not the text
		ImGui::ProgressBar(percent, ImVec2(ImGui::GetWindowWidth() * 0.9f, 0.0f), "");
	} else {
		// Display a fallback message if we couldn't get the VRAM info
		ImGui::Text("VRAM Usage: Not available");
	}
}

void PerformanceOverlay::DrawPostFGFrameTimeGraph()
{
	// Prepare overlay text
	char overlay_text[128];
	snprintf(overlay_text, IM_ARRAYSIZE(overlay_text),
		"Post-FG: %.2f ms (%.1f FPS)",
		state.postFGSmoothFrameTimeMs, state.postFGSmoothFps);

	// Set graph colors - blue for post-FG
	ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));  // Blue line

	// Draw the graph
	float graphWidth = ImGui::GetWindowWidth() * 0.9f;
	ImGui::PlotLines("##postfgframetime",
		state.postFGFrameTimeHistory.GetData().data(),
		settings.FrameHistorySize,
		static_cast<int>(state.postFGFrameTimeHistory.GetHeadIdx()),
		overlay_text,
		state.smoothedMinFrameTime, state.smoothedMaxFrameTime,
		ImVec2(graphWidth, 50.0f * Util::GetUIScale() * settings.TextSize));

	ImGui::PopStyleColor();

	// Draw frametime target reference lines
	if (ImGui::BeginTable("PostFGFrametimeTargets", 3, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		ImGui::Text("30 FPS: 33.3 ms");

		ImGui::TableNextColumn();
		ImGui::Text("60 FPS: 16.7 ms");

		ImGui::TableNextColumn();
		ImGui::Text("120 FPS: 8.3 ms");

		ImGui::EndTable();
	}
}

// ============================================================================
// A/B TESTING FUNCTIONS
// ============================================================================

// --- ABTestAggregator integration ---
ABTestAggregator& PerformanceOverlay::GetABTestAggregator()
{
	auto* abTestingManager = ABTestingManager::GetSingleton();
	return abTestingManager->GetAggregator();
}

/**
  * @brief Draws the A/B test results table with comprehensive performance comparison
  *
  * This function renders a detailed table showing performance metrics for both Variant A (USER config)
  * and Variant B (TEST config), including:
  * - Average and median frame times for each shader type
  * - Performance deltas and percentage differences
  * - Color-coded indicators for better/worse performance
  * - Statistical validity assessment with tooltips
  * - Sortable columns for easy analysis
  *
  * The table provides both main rows (individual shader types) and summary rows (Total, Other)
  * to give users a complete picture of performance differences between configurations.
  *
  * @note This function requires an active A/B test with aggregated results
  */
void PerformanceOverlay::DrawABTestResultsTable()
{
	auto* abTestingManager = ABTestingManager::GetSingleton();
	auto& aggregator = abTestingManager->GetAggregator();
	auto results = aggregator.GetAggregatedResults();
	if (results.empty())
		return;

	auto* menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();

	DrawABTestStatisticalValidity(theme, aggregator);

	std::vector<DrawCallRow> mainRows, summaryRows;
	ConvertABTestResultsToRows(results, mainRows, summaryRows);

	ABTestLegends legends = BuildABTestLegends(theme);

	auto columns = BuildABTestResultsTableColumns(theme, legends);

	std::vector<std::function<bool(const DrawCallRow&, const DrawCallRow&, bool)>> sorters;
	for (const auto& col : columns) sorters.push_back(col.sortFunc);
	std::vector<DrawCallRow> mainRowsCopy = mainRows;
	std::vector<DrawCallRow> summaryRowsCopy = summaryRows;
	Util::ShowSortedStringTableCustom<DrawCallRow>(
		"ABTestResultsTable",
		[&columns]() { std::vector<std::string> h; for (const auto& c : columns) h.push_back(c.header); return h; }(),
		mainRowsCopy,
		0,     // Default sort column (Shader Type)
		true,  // Default ascending
		sorters,
		[&columns](int rowIdx, int colIdx, const DrawCallRow& row) {
			(void)rowIdx;
			columns[colIdx].cellRender(row, colIdx);
		},
		summaryRowsCopy);
}

/**
  * @brief Draws statistical validity information for A/B test results
  *
  * This function displays test duration, valid frame counts, and exclusion rates
  * with color-coded indicators for statistical validity. It helps users understand
  * whether the A/B test results are reliable and statistically significant.
  *
  * @param theme The current UI theme settings
  * @param aggregator The A/B test aggregator containing test statistics
  */
void PerformanceOverlay::DrawABTestStatisticalValidity(const Menu::ThemeSettings& theme, const ABTestAggregator& aggregator) const
{
	float totalDuration = aggregator.GetTotalTestDuration();
	int totalFrames = aggregator.GetTotalFrameCount();
	int excludedFrames = 0;
	for (const auto& interval : aggregator.GetIntervals()) {
		excludedFrames += interval.excludedFrames;
	}
	int validFrames = totalFrames;
	int totalWithExcluded = totalFrames + excludedFrames;
	float validPercent = (totalWithExcluded > 0) ? (100.0f * validFrames / totalWithExcluded) : 100.0f;

	bool hasEnoughSamples = validFrames >= kMinimumSamplesForValidity;
	bool hasGoodDuration = totalDuration >= kMinimumTestDuration;
	bool hasLowExclusionRate = validPercent >= kMinimumValidFramesPercent;
	bool isStatisticallyValid = hasEnoughSamples && hasGoodDuration && hasLowExclusionRate;

	ImVec4 validityColor = theme.Palette.Text;
	if (isStatisticallyValid) {
		validityColor = theme.StatusPalette.SuccessColor;
	} else if (validFrames >= kMinimumSamplesForMarginal && totalDuration >= kMinimumDurationForMarginal) {
		validityColor = theme.StatusPalette.Warning;
	} else {
		validityColor = theme.StatusPalette.Error;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, validityColor);
	ImGui::Text("Test Duration: %.1f seconds | Valid Frames: %d/%d (%.1f%%) | Excluded: %d",
		totalDuration, validFrames, totalWithExcluded, validPercent, excludedFrames);
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered()) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			char validStr[128], marginalStr[128];
			snprintf(validStr, sizeof(validStr), "Statistically valid (>%d samples, >%.0fs duration, >%.0f%% valid)", kMinimumSamplesForValidity, static_cast<float>(kMinimumTestDuration), kMinimumValidFramesPercent);
			snprintf(marginalStr, sizeof(marginalStr), "Marginal validity (>%d samples, >%.0fs duration)", kMinimumSamplesForMarginal, static_cast<float>(kMinimumDurationForMarginal));
			Util::ColoredTextLines validityLegend = {
				{ "Valid frames are those not excluded as outliers.\nA low percentage may indicate instability or test interruptions.\nExcluded frames are those with frame times > 3x median or > 100ms.\nThis removes shader compilation spikes, JSON loading overhead, and other anomalies\nthat would skew the performance comparison.", theme.Palette.Text },
				{ "", theme.Palette.Text },
				{ validStr, theme.StatusPalette.SuccessColor },
				{ marginalStr, theme.StatusPalette.Warning },
				{ "Insufficient data for reliable results", theme.StatusPalette.Error }
			};
			Util::DrawColoredMultiLineTooltip(validityLegend);
		}
	}
}

/**
  * @brief Converts A/B test aggregated results into table rows
  *
  * This function transforms aggregated A/B test statistics into DrawCallRow structures
  * suitable for display in the performance overlay table. It separates main shader type
  * rows from summary rows (Total, Other) and assigns appropriate tooltips.
  *
  * @param results The aggregated A/B test results
  * @param mainRows Output vector for individual shader type rows
  * @param summaryRows Output vector for summary rows (Total, Other)
  */
void PerformanceOverlay::ConvertABTestResultsToRows(const std::vector<AggregatedDrawCallStats>& results, std::vector<DrawCallRow>& mainRows, std::vector<DrawCallRow>& summaryRows) const
{
	mainRows.clear();
	summaryRows.clear();
	for (const auto& stat : results) {
		DrawCallRow row;
		row.label = stat.label;
		row.shaderType = stat.shaderType;
		row.frameTime = stat.meanA;
		row.percent = (stat.meanA > 0.0f) ? (stat.meanA / (stat.meanA + stat.meanB) * 100.0f) : 0.0f;
		row.costPerCall = stat.medianA;
		row.enabled = true;
		row.testFrameTime = stat.meanB;
		row.testCostPerCall = stat.medianB;
		if (row.shaderType >= 0) {
			auto shaderType = static_cast<RE::BSShader::Type>(row.shaderType);
			auto tipIt = kShaderTypeTooltips.find(shaderType);
			if (tipIt != kShaderTypeTooltips.end()) {
				row.tooltip = tipIt->second;
			} else {
				row.tooltip = "Draw calls for this shader type.";
			}
		} else {
			auto maybeSpecialType = magic_enum::enum_cast<SpecialShaderType>(row.shaderType);
			if (maybeSpecialType.has_value()) {
				switch (*maybeSpecialType) {
				case SpecialShaderType::Total:
					row.tooltip = "Total frame time.";
					break;
				case SpecialShaderType::Other:
					row.tooltip = "Frame time not attributed to any measured shader type. This includes UI, post-processing, engine work, and any GPU activity not directly measured by the overlay.";
					break;
				}
			}
		}
		if (row.shaderType < 0) {
			summaryRows.push_back(row);
		} else {
			mainRows.push_back(row);
		}
	}
}

/**
  * @brief Builds color-coded legends for A/B test table columns
  *
  * This function creates comprehensive tooltip legends for each A/B test column,
  * explaining the meaning of colors and values. The legends help users understand
  * performance comparisons between Variant A (USER) and Variant B (TEST) configurations.
  *
  * @param theme The current UI theme settings
  * @return ABTestLegends structure containing all column legends
  */
ABTestLegends PerformanceOverlay::BuildABTestLegends(const Menu::ThemeSettings& theme) const
{
	ABTestLegends legends;

	legends.shaderType = {
		"Shader Type",
		{ { "Shader Type: The type of shader being measured.", theme.Palette.Text },
			{ "Click to toggle shader on/off for performance testing.", theme.Palette.Text } }
	};

	legends.aAvg = {
		"A Avg (ms)",
		{ { "A Avg (ms): Average frame time for Variant A (USER config).", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (compared to Variant B):", theme.Palette.Text },
			{ "  Better (lower than B)", theme.StatusPalette.SuccessColor },
			{ "  Worse (higher than B)", theme.StatusPalette.Error },
			{ "  Same as B", theme.Palette.Text } }
	};

	legends.bAvg = {
		"B Avg (ms)",
		{ { "B Avg (ms): Average frame time for Variant B (TEST config).", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (compared to Variant A):", theme.Palette.Text },
			{ "  Better (lower than A)", theme.StatusPalette.SuccessColor },
			{ "  Worse (higher than A)", theme.StatusPalette.Error },
			{ "  Same as A", theme.Palette.Text } }
	};

	legends.delta = {
		"Delta (ms)",
		{ { "Delta (ms): Difference between Variant B and Variant A (B - A).", theme.Palette.Text },
			{ "Negative values indicate Variant B is better (lower frame time).", theme.Palette.Text },
			{ "Positive values indicate Variant A is better (lower frame time).", theme.Palette.Text },
			{ "Percentage shows relative performance difference.", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend:", theme.Palette.Text },
			{ "  Negative (B better)", theme.StatusPalette.SuccessColor },
			{ "  Positive (A better)", theme.StatusPalette.Error },
			{ "  Zero (same)", theme.Palette.Text } }
	};

	legends.aMedian = {
		"A Median (ms)",
		{ { "A Median: Median frame time for Variant A (USER config).", theme.Palette.Text },
			{ "Median is less sensitive to outliers than average.", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (compared to Variant B median):", theme.Palette.Text },
			{ "  Better (lower than B)", theme.StatusPalette.SuccessColor },
			{ "  Worse (higher than B)", theme.StatusPalette.Error },
			{ "  Same as B", theme.Palette.Text } }
	};

	legends.bMedian = {
		"B Median (ms)",
		{ { "B Median: Median frame time for Variant B (TEST config).", theme.Palette.Text },
			{ "Median is less sensitive to outliers than average.", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (compared to Variant A median):", theme.Palette.Text },
			{ "  Better (lower than A)", theme.StatusPalette.SuccessColor },
			{ "  Worse (higher than A)", theme.StatusPalette.Error },
			{ "  Same as A", theme.Palette.Text } }
	};

	legends.medianDelta = {
		"Median Delta (ms)",
		{ { "Median Delta: Difference between Variant B and Variant A medians (B - A).", theme.Palette.Text },
			{ "Negative values indicate Variant B is better (lower median).", theme.Palette.Text },
			{ "Positive values indicate Variant A is better (lower median).", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend:", theme.Palette.Text },
			{ "  Negative (B better)", theme.StatusPalette.SuccessColor },
			{ "  Positive (A better)", theme.StatusPalette.Error },
			{ "  Zero (same)", theme.Palette.Text } }
	};

	return legends;
}

/**
  * @brief Builds column configurations for the A/B test results table
  *
  * This function creates column configurations for displaying A/B test results,
  * including average and median frame times for both variants, performance deltas,
  * and color-coded indicators for better/worse performance comparisons.
  *
  * @param theme The current UI theme settings
  * @param legends The color-coded legends for tooltips
  * @return Vector of column configurations for the table
  */
std::vector<ColumnConfig> PerformanceOverlay::BuildABTestResultsTableColumns(const Menu::ThemeSettings& theme, const ABTestLegends& legends) const
{
	std::vector<ColumnConfig> columns = {
		{ legends.shaderType.header,
			[theme](const DrawCallRow& row, int) {
				ImGui::TextUnformatted(row.label.c_str());
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(row.tooltip.c_str());
						// Add FPS for Total row
						if (row.label == "Total:") {
							float fps = row.frameTime > 0.0f ? 1000.0f / row.frameTime : 0.0f;
							ImGui::Text("FPS: %.2f", fps);
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.label < b.label) : (a.label > b.label); },
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.shaderType.tooltip);
					}
				}
			} },
		{ legends.aAvg.header,
			[theme, legends](const DrawCallRow& row, int) {
				float value = row.frameTime;
				// Color A relative to B
				ImVec4 color = theme.Palette.Text;
				if (row.testFrameTime.has_value()) {
					if (value < *row.testFrameTime) {
						color = theme.StatusPalette.SuccessColor;  // A is better (lower) than B
					} else if (value > *row.testFrameTime) {
						color = theme.StatusPalette.Error;  // A is worse (higher) than B
					}
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s", Util::FormatMilliseconds(value).c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.label == "Total:") {
							ImGui::Text("A (USER) FPS: %.2f", Util::CalcFPS(value));
						} else {
							Util::DrawColoredMultiLineTooltip(legends.aAvg.tooltip);
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.frameTime < b.frameTime) : (a.frameTime > b.frameTime); },
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.aAvg.tooltip);
					}
				}
			} },
		{ legends.bAvg.header,
			[theme, legends](const DrawCallRow& row, int) {
				if (!row.testFrameTime.has_value()) {
					ImGui::TextDisabled("-");
					return;
				}
				float value = *row.testFrameTime;
				// Color B relative to A
				ImVec4 color = theme.Palette.Text;
				if (value < row.frameTime) {
					color = theme.StatusPalette.SuccessColor;  // B is better (lower) than A
				} else if (value > row.frameTime) {
					color = theme.StatusPalette.Error;  // B is worse (higher) than A
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s", Util::FormatMilliseconds(value).c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.label == "Total:") {
							ImGui::Text("B (TEST) FPS: %.2f", Util::CalcFPS(value));
						} else {
							Util::DrawColoredMultiLineTooltip(legends.bAvg.tooltip);
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				float aVal = a.testFrameTime.value_or(FLT_MAX);
				float bVal = b.testFrameTime.value_or(FLT_MAX);
				return asc ? (aVal < bVal) : (aVal > bVal);
			},
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.bAvg.tooltip);
					}
				}
			} },
		{ legends.delta.header,
			[theme, legends](const DrawCallRow& row, int) {
				if (!row.testFrameTime.has_value()) {
					ImGui::TextDisabled("-");
					return;
				}
				float delta = *row.testFrameTime - row.frameTime;
				// Color based on delta
				ImVec4 color = theme.Palette.Text;
				if (delta < 0.0f) {
					color = theme.StatusPalette.SuccessColor;  // Better performance (negative delta)
				} else if (delta > 0.0f) {
					color = theme.StatusPalette.Error;  // Worse performance (positive delta)
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s", Util::FormatDeltaWithPercent(row.frameTime, *row.testFrameTime, PerformanceOverlay::Settings::kPercentDisplayThreshold).c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.testFrameTime.has_value()) {
							// Show detailed values for rows with test data
							if (row.label == "Total:") {
								ImGui::TextUnformatted("Delta (B - A):");
								ImGui::Separator();
								ImGui::Text("A (USER) FPS: %.2f", Util::CalcFPS(row.frameTime));
								ImGui::Text("B (TEST) FPS: %.2f", Util::CalcFPS(*row.testFrameTime));
							} else {
								ImGui::TextUnformatted("Delta (B - A):");
								ImGui::Separator();
								ImGui::Text("A (USER): %.3f ms", row.frameTime);
								ImGui::Text("B (TEST): %.3f ms", *row.testFrameTime);
							}
							ImGui::Separator();
						}
						// Always show the delta legend for explanation
						Util::DrawColoredMultiLineTooltip(legends.delta.tooltip);
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				float aDelta = a.testFrameTime.value_or(0.0f) - a.frameTime;
				float bDelta = b.testFrameTime.value_or(0.0f) - b.frameTime;
				return asc ? (aDelta < bDelta) : (aDelta > bDelta);
			},
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.delta.tooltip);
					}
				}
			} },
		{ legends.aMedian.header,
			[theme, legends](const DrawCallRow& row, int) {
				float value = row.costPerCall;
				// Color A median relative to B median (stored in testCostPerCall for now)
				ImVec4 color = theme.Palette.Text;
				if (row.testCostPerCall.has_value()) {
					if (value < *row.testCostPerCall) {
						color = theme.StatusPalette.SuccessColor;  // A is better (lower) than B
					} else if (value > *row.testCostPerCall) {
						color = theme.StatusPalette.Error;  // A is worse (higher) than B
					}
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s", Util::FormatMilliseconds(value).c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.label == "Total:") {
							Util::ColoredTextLines fpsTooltip{
								{ std::format("A (USER) Median FPS: {:.2f}", Util::CalcFPS(value)), ImVec4(1.0f, 1.0f, 1.0f, 1.0f) }
							};
							Util::DrawColoredMultiLineTooltip(fpsTooltip);
						} else {
							Util::DrawColoredMultiLineTooltip(legends.aMedian.tooltip);
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.costPerCall < b.costPerCall) : (a.costPerCall > b.costPerCall); },
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.aMedian.tooltip);
					}
				}
			} },
		{ legends.bMedian.header,
			[theme, legends](const DrawCallRow& row, int) {
				if (!row.testCostPerCall.has_value()) {
					ImGui::TextDisabled("-");
					return;
				}
				float value = *row.testCostPerCall;
				// Color B median relative to A median
				ImVec4 color = theme.Palette.Text;
				if (value < row.costPerCall) {
					color = theme.StatusPalette.SuccessColor;  // B is better (lower) than A
				} else if (value > row.costPerCall) {
					color = theme.StatusPalette.Error;  // B is worse (higher) than A
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s", Util::FormatMilliseconds(value).c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.label == "Total:") {
							Util::ColoredTextLines fpsTooltip{
								{ std::format("B (TEST) Median FPS: {:.2f}", Util::CalcFPS(value)), ImVec4(1.0f, 1.0f, 1.0f, 1.0f) }
							};
							Util::DrawColoredMultiLineTooltip(fpsTooltip);
						} else {
							Util::DrawColoredMultiLineTooltip(legends.bMedian.tooltip);
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				float aVal = a.testCostPerCall.value_or(FLT_MAX);
				float bVal = b.testCostPerCall.value_or(FLT_MAX);
				return asc ? (aVal < bVal) : (aVal > bVal);
			},
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.bMedian.tooltip);
					}
				}
			} },
		{ legends.medianDelta.header,
			[theme, legends](const DrawCallRow& row, int) {
				if (!row.testCostPerCall.has_value()) {
					ImGui::TextDisabled("-");
					return;
				}
				float delta = *row.testCostPerCall - row.costPerCall;
				// Color based on delta
				ImVec4 color = theme.Palette.Text;
				if (delta < 0.0f) {
					color = theme.StatusPalette.SuccessColor;  // Better performance (negative delta)
				} else if (delta > 0.0f) {
					color = theme.StatusPalette.Error;  // Worse performance (positive delta)
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				std::string deltaStr = (delta > 0.0f) ? "+" + Util::FormatMilliseconds(delta) : Util::FormatMilliseconds(delta);
				ImGui::Text("%s", deltaStr.c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.label == "Total:" && row.testCostPerCall.has_value()) {
							Util::ColoredTextLines fpsTooltip{
								{ "Median Delta (B - A):", ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
								{ "", ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
								{ std::format("A (USER) Median FPS: {:.2f}", Util::CalcFPS(row.costPerCall)), ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
								{ std::format("B (TEST) Median FPS: {:.2f}", Util::CalcFPS(*row.testCostPerCall)), ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
								{ "", ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
								{ "Median is less sensitive to outliers than average.", ImVec4(1.0f, 1.0f, 1.0f, 1.0f) }
							};
							Util::DrawColoredMultiLineTooltip(fpsTooltip);
						} else {
							Util::DrawColoredMultiLineTooltip(legends.medianDelta.tooltip);
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				float aDelta = a.testCostPerCall.value_or(0.0f) - a.costPerCall;
				float bDelta = b.testCostPerCall.value_or(0.0f) - b.costPerCall;
				return asc ? (aDelta < bDelta) : (aDelta > bDelta);
			},
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.medianDelta.tooltip);
					}
				}
			} }
	};

	return columns;
}

/**
  * @brief Draws the A/B testing section of the performance overlay
  *
  * This function handles all A/B testing related UI including:
  * - A/B test state management and data collection
  * - Display of aggregated A/B test results
  * - Settings difference comparison table
  * - A/B test controls (clear results, show/hide settings diff)
  *
  * @param allRows The current draw call rows for data collection
  */
void PerformanceOverlay::DrawABTestSection(const std::vector<DrawCallRow>& allRows)
{
	auto* menu = Menu::GetSingleton();
	auto* abTestingManager = ABTestingManager::GetSingleton();
	bool abTestingEnabled = abTestingManager && abTestingManager->IsEnabled();
	static ABVariant lastVariant = ABVariant::A;
	static bool lastUsingTestConfig = false;
	static bool wasAbTestActive = false;
	bool currentUsingTestConfig = abTestingManager && abTestingManager->IsUsingTestConfig();
	static std::string lastSettingsA, lastSettingsB;
	std::string currentSettingsA, currentSettingsB;
	auto& aggregator = abTestingManager->GetAggregator();
	if (abTestingEnabled) {
		// Serialize current settings for A and B from the aggregator
		if (aggregator.HasSettingsA())
			currentSettingsA = aggregator.GetSettingsA().dump();
		if (aggregator.HasSettingsB())
			currentSettingsB = aggregator.GetSettingsB().dump();
	}
	// Detect A/B test start/stop and variant switches
	bool settingsChanged = (currentSettingsA != lastSettingsA) || (currentSettingsB != lastSettingsB);
	if (abTestingEnabled && (!wasAbTestActive || settingsChanged)) {
		aggregator.Clear();
		aggregator.OnABSwitch(currentUsingTestConfig ? ABVariant::B : ABVariant::A);
		lastSettingsA = currentSettingsA;
		lastSettingsB = currentSettingsB;
	}
	if (abTestingEnabled && (currentUsingTestConfig != lastUsingTestConfig)) {
		aggregator.OnABSwitch(currentUsingTestConfig ? ABVariant::B : ABVariant::A);
	}
	if (!abTestingEnabled && wasAbTestActive) {
		aggregator.OnTestEnd();
	}
	wasAbTestActive = abTestingEnabled;
	lastUsingTestConfig = currentUsingTestConfig;

	// --- A/B Test Data Collection ---
	if (abTestingEnabled) {
		aggregator.OnFrame(allRows);  // Pass both main and summary rows
	}

	// Display A/B test results if available
	if (aggregator.HasResults()) {
		this->DrawABTestResultsTable();
		ImGui::Separator();
		// --- A/B Results Controls ---
		static bool showSettingsDiff = false;
		ImGui::BeginGroup();
		if (ImGui::Button(showSettingsDiff ? "Hide Settings Diff" : "Show Settings Diff")) {
			showSettingsDiff = !showSettingsDiff;
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear A/B Test Results")) {
			aggregator.Clear();
			this->settingsDiff.clear();
			this->settingsDiffLoaded = false;
			showSettingsDiff = false;
			// Also clear cached snapshots so diff does not linger after user opts to clear
			if (abTestingManager) {
				abTestingManager->ClearCachedSnapshots();
			}
			ImGui::EndGroup();
			ImGui::Separator();
			return;
		}
		ImGui::EndGroup();
		// --- Settings diff section (inline, toggled) ---
		if (showSettingsDiff) {
			if (!this->settingsDiffLoaded) {
				// Use cached memory data from ABTestingManager instead of loading from disk
				if (abTestingManager && abTestingManager->HasCachedSnapshots()) {
					// Pull structured diff entries directly from ABTestingManager (in-memory snapshots)
					this->settingsDiff = abTestingManager->GetConfigDiffEntries();
				} else {
					std::filesystem::path userPath = Util::PathHelpers::GetDataPath() / "SKSE/Plugins/CommunityShaders/SettingsUser.json";
					std::filesystem::path testPath = Util::PathHelpers::GetDataPath() / "SKSE/Plugins/CommunityShaders/SettingsTest.json";
					this->settingsDiff = Util::FileSystem::LoadJsonDiff(userPath, testPath);
				}
				this->settingsDiffLoaded = true;
			}
			ImGui::TextUnformatted("Differences between USER (A) and TEST (B) configs:");
			if (this->settingsDiff.empty()) {
				ImGui::TextUnformatted("No setting changes detected between USER (A) and TEST (B) configs.");
			} else if (ImGui::BeginTable("ABSettingsDiffTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
				ImGui::TableSetupColumn("Setting Path", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("A Value");
				ImGui::TableSetupColumn("B Value");
				ImGui::TableHeadersRow();

				// Determine which variant performed better based on Total row
				bool variantABetter = false;
				bool variantBBetter = false;
				auto results = aggregator.GetAggregatedResults();
				for (const auto& stat : results) {
					auto maybeSpecialType = magic_enum::enum_cast<SpecialShaderType>(stat.shaderType);
					if (maybeSpecialType.has_value() && *maybeSpecialType == SpecialShaderType::Total) {  // Total row
						if (stat.meanA < stat.meanB) {
							variantABetter = true;  // A has lower frame time (better)
						} else if (stat.meanB < stat.meanA) {
							variantBBetter = true;  // B has lower frame time (better)
						}
						break;
					}
				}

				// Get theme for color coding
				const auto& theme = menu->GetTheme();

				// Sort the settings diff if needed
				std::vector<SettingsDiffEntry> sortedDiff = this->settingsDiff;
				if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
					if (sortSpecs->SpecsCount > 0) {
						int sortCol = sortSpecs->Specs->ColumnIndex;
						bool sortAsc = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
						std::sort(sortedDiff.begin(), sortedDiff.end(), [sortCol, sortAsc](const SettingsDiffEntry& a, const SettingsDiffEntry& b) {
							if (sortCol == 0)
								return sortAsc ? (a.path < b.path) : (a.path > b.path);
							if (sortCol == 1)
								return sortAsc ? (a.aValue < b.aValue) : (a.aValue > b.aValue);
							if (sortCol == 2)
								return sortAsc ? (a.bValue < b.bValue) : (a.bValue > b.bValue);
							return false;
						});
					}
				}
				for (const auto& entry : sortedDiff) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(entry.path.c_str());
					// Only show the path as text, no custom tooltip guessing
					ImGui::TableSetColumnIndex(1);
					// Color A value based on performance
					if (variantABetter) {
						ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.SuccessColor);
						ImGui::TextUnformatted(entry.aValue.c_str());
						ImGui::PopStyleColor();
					} else if (variantBBetter) {
						ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.Error);
						ImGui::TextUnformatted(entry.aValue.c_str());
						ImGui::PopStyleColor();
					} else {
						ImGui::TextUnformatted(entry.aValue.c_str());
					}
					ImGui::TableSetColumnIndex(2);
					// Color B value based on performance
					if (variantBBetter) {
						ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.SuccessColor);
						ImGui::TextUnformatted(entry.bValue.c_str());
						ImGui::PopStyleColor();
					} else if (variantABetter) {
						ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.Error);
						ImGui::TextUnformatted(entry.bValue.c_str());
						ImGui::PopStyleColor();
					} else {
						ImGui::TextUnformatted(entry.bValue.c_str());
					}
				}
				ImGui::EndTable();
			}
			ImGui::Separator();
		}
	}
}
// ============================================================================
// TABLE BUILDING AND RENDERING FUNCTIONS
// ============================================================================

// Private helper for table rendering
void PerformanceOverlay::DrawDrawCallsTable(const std::vector<DrawCallRow>& mainRows, const std::vector<DrawCallRow>& summaryRows)
{
	static bool clearTestDataRequested = false;
	auto& overlay = globals::features::performanceOverlay;
	auto* menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();

	// Capture test data and handle clear button
	overlay.CaptureTestData();
	bool anyTestData = !overlay.testData.empty();
	if (anyTestData) {
		if (ImGui::Button("Clear Test Data")) {
			clearTestDataRequested = true;
		}
	}

	// Build legends and column configurations
	auto legends = overlay.BuildDrawCallLegends(theme, anyTestData);
	auto columns = overlay.BuildDrawCallTableColumns(theme, legends, anyTestData);

	// Build sorters
	std::vector<std::function<bool(const DrawCallRow&, const DrawCallRow&, bool)>> sorters;
	for (const auto& col : columns) sorters.push_back(col.sortFunc);

	// Create non-const copies for the table function
	std::vector<DrawCallRow> mainRowsCopy = mainRows;
	std::vector<DrawCallRow> summaryRowsCopy = summaryRows;

	// Create table row handler
	auto rowHandler = overlay.CreateTableRowHandler(columns);

	// Render the table
	Util::ShowSortedStringTableCustom<DrawCallRow>(
		"DrawCallOverlayTable",
		[&columns]() { std::vector<std::string> h; for (const auto& c : columns) h.push_back(c.header); return h; }(),
		mainRowsCopy,
		0,     // Default sort column (Shader Type)
		true,  // Default ascending
		sorters,
		rowHandler,
		summaryRowsCopy);

	// Handle clear test data request
	if (clearTestDataRequested) {
		overlay.ClearTestData();
		clearTestDataRequested = false;
	}
}

DrawCallLegends PerformanceOverlay::BuildDrawCallLegends(const Menu::ThemeSettings& theme, bool anyTestData) const
{
	(void)anyTestData;
	DrawCallLegends legends;

	legends.shaderType = {
		"Shader Type",
		{ { "Shader Type: The type of shader being measured.", theme.Palette.Text },
			{ "Click to toggle shader on/off for performance testing.", theme.Palette.Text } }
	};

	legends.drawCalls = {
		"Draw Calls",
		{ { "Draw Calls: Number of draw calls for this shader type in the current frame.", theme.Palette.Text } }
	};

	legends.frameTime = {
		"Frame Time (%)",
		{ { GetTestDataTooltip(), theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Performance Color Legend (ms):", theme.Palette.Text },
			{ "  <= 2 ms", theme.StatusPalette.SuccessColor },
			{ "  > 2 ms and <= 5 ms", theme.StatusPalette.Warning },
			{ "  > 5 ms", theme.StatusPalette.Error } }
	};

	legends.costPerCall = {
		"Cost/Call",
		{ { "Cost/Call: Average time per draw call for this shader type.", theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (ms/call):", theme.Palette.Text },
			{ "  <= 0.05 ms/call", theme.StatusPalette.SuccessColor },
			{ "  > 0.05 ms and <= 0.2 ms/call", theme.StatusPalette.Warning },
			{ "  > 0.2 ms/call", theme.StatusPalette.Error } }
	};

	legends.testFrameTime = {
		"Test Frame Time (%)",
		{ { GetTestDataTooltip(), theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (compared to live data):", theme.Palette.Text },
			{ "  Better (lower than live)", theme.StatusPalette.SuccessColor },
			{ "  Worse (higher than live)", theme.StatusPalette.Error },
			{ "  Same as live", theme.Palette.Text } }
	};

	legends.testCostPerCall = {
		"Test Cost/Call",
		{ { GetTestDataTooltip(), theme.Palette.Text },
			{ "", theme.Palette.Text },
			{ "Color Legend (compared to live data):", theme.Palette.Text },
			{ "  Better (lower than live)", theme.StatusPalette.SuccessColor },
			{ "  Worse (higher than live)", theme.StatusPalette.Error },
			{ "  Same as live", theme.Palette.Text } }
	};

	return legends;
}

std::vector<ColumnConfig> PerformanceOverlay::BuildDrawCallTableColumns(const Menu::ThemeSettings& theme, const DrawCallLegends& legends, bool anyTestData)
{
	// Build column configurations
	std::vector<ColumnConfig> columns = {
		{ legends.shaderType.header,
			[theme, this](const DrawCallRow& row, int) {
				if (!row.enabled)
					ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.Disable);
				bool wasEnabled = row.enabled;
				if (ImGui::Selectable(row.label.c_str(), false)) {
					auto maybeType = magic_enum::enum_cast<RE::BSShader::Type>(row.shaderType);
					if (maybeType.has_value()) {
						auto classIndex = magic_enum::enum_integer(*maybeType) - 1;
						if (classIndex >= 0 && classIndex < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1) {
							HandleShaderToggle(row, wasEnabled);
						}
					}
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(row.tooltip.c_str());
					}
				}
				if (!row.enabled)
					ImGui::PopStyleColor();
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.label < b.label) : (a.label > b.label); },
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.shaderType.tooltip);
					}
				}
			} },
		{ legends.drawCalls.header,
			[](const DrawCallRow& row, int) {
				if (row.drawCalls == kDrawCallsNotApplicable) {
					ImGui::TextDisabled("-");
				} else {
					ImGui::Text("%d", row.drawCalls);
				}
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						if (row.drawCalls == kDrawCallsNotApplicable) {
							ImGui::TextUnformatted("Draw Calls: Not applicable for unmeasured GPU time.");
						} else {
							ImGui::TextUnformatted("Draw Calls: Number of draw calls for this shader type in the current frame.");
						}
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.drawCalls < b.drawCalls) : (a.drawCalls > b.drawCalls); },
			[legends]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						Util::DrawColoredMultiLineTooltip(legends.drawCalls.tooltip);
					}
				}
			} }
	};

	columns.push_back(ColumnConfig{
		legends.frameTime.header,
		MakeMetricColumn(theme, [](const DrawCallRow& row) { return row.frameTime; }, [](const auto& theme, float value, const DrawCallRow& row) {
			if (magic_enum::enum_cast<SpecialShaderType>(row.shaderType).has_value()) {
				return theme.Palette.Text;
			}
			return Util::GetThresholdColor(value, PerformanceOverlay::Settings::kFrameTimeGoodThreshold, PerformanceOverlay::Settings::kFrameTimeWarningThreshold, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error); }, [](float /*value*/, const DrawCallRow& row) { return Util::FormatMilliseconds(row.frameTime) + " (" + Util::FormatPercent(row.percent) + ")"; }, legends.frameTime.tooltip), [](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.percent < b.percent) : (a.percent > b.percent); }, [legends]() {
			 if (ImGui::IsItemHovered()) {
				 if (auto _tt = Util::HoverTooltipWrapper()) {
					 Util::DrawColoredMultiLineTooltip(legends.frameTime.tooltip);
				 }
			 } } });

	columns.push_back(ColumnConfig{
		legends.costPerCall.header,
		MakeMetricColumn(theme, [](const DrawCallRow& row) { return row.costPerCall; }, [](const auto& theme, float value, const DrawCallRow& row) {
			if (magic_enum::enum_cast<SpecialShaderType>(row.shaderType).has_value()) {
				return theme.Palette.Text;
			}
			return Util::GetThresholdColor(value, PerformanceOverlay::Settings::kCostPerCallGoodThreshold, PerformanceOverlay::Settings::kCostPerCallWarningThreshold, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error); }, [](float value, const DrawCallRow&) { return (value < PerformanceOverlay::Settings::kMicrosecondThreshold && value > 0.0f) ? Util::FormatMicroseconds(value * 1000.0f) : Util::FormatMilliseconds(value); }, legends.costPerCall.tooltip), [](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.costPerCall < b.costPerCall) : (a.costPerCall > b.costPerCall); }, [legends]() {
			 if (ImGui::IsItemHovered()) {
				 if (auto _tt = Util::HoverTooltipWrapper()) {
					 Util::DrawColoredMultiLineTooltip(legends.costPerCall.tooltip);
				 }
			 } } });

	// Add test columns if present
	if (anyTestData) {
		columns.push_back(ColumnConfig{
			legends.testFrameTime.header,
			MakeMetricColumn(theme, [](const DrawCallRow& row) { return row.testFrameTime; }, [](const auto& theme, float value, const DrawCallRow& row) {
					 if (value < row.frameTime)
						 return theme.StatusPalette.SuccessColor;
					 if (value > row.frameTime)
						 return theme.StatusPalette.Error;
					 return theme.Palette.Text; }, [this](float value, const DrawCallRow& row) { return Util::FormatMilliseconds(value) + " (" + Util::FormatPercent(testData[row.shaderType].percent) + ")"; }, legends.testFrameTime.tooltip), [](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				 float aVal = a.testFrameTime.value_or(FLT_MAX);
				 float bVal = b.testFrameTime.value_or(FLT_MAX);
				 return asc ? (aVal < bVal) : (aVal > bVal); }, [legends]() {
				 if (ImGui::IsItemHovered()) {
					 if (auto _tt = Util::HoverTooltipWrapper()) {
						 Util::DrawColoredMultiLineTooltip(legends.testFrameTime.tooltip);
					 }
				 } } });

		columns.push_back(ColumnConfig{
			legends.testCostPerCall.header,
			MakeMetricColumn(theme, [](const DrawCallRow& row) { return row.testCostPerCall; }, [](const auto& theme, float value, const DrawCallRow& row) {
					 if (value < row.costPerCall)
						 return theme.StatusPalette.SuccessColor;
					 if (value > row.costPerCall)
						 return theme.StatusPalette.Error;
					 return theme.Palette.Text; }, [](float value, const DrawCallRow&) { return (value < PerformanceOverlay::Settings::kMicrosecondThreshold && value > 0.0f) ? Util::FormatMicroseconds(value * 1000.0f) : Util::FormatMilliseconds(value); }, legends.testCostPerCall.tooltip), [](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				 float aVal = a.testCostPerCall.value_or(FLT_MAX);
				 float bVal = b.testCostPerCall.value_or(FLT_MAX);
				 return asc ? (aVal < bVal) : (aVal > bVal); }, [legends]() {
				 if (ImGui::IsItemHovered()) {
					 if (auto _tt = Util::HoverTooltipWrapper()) {
						 Util::DrawColoredMultiLineTooltip(legends.testCostPerCall.tooltip);
					 }
				 } } });
	}

	return columns;
}

std::pair<std::vector<DrawCallRow>, std::vector<DrawCallRow>> PerformanceOverlay::BuildDrawCallRows() const
{
	std::vector<DrawCallRow> mainRows;
	float smoothedFrameTime = static_cast<float>(this->state.smoothFrameTimeMs);
	float measuredSum = 0.0f;

	globals::state->ForEachShaderTypeWithMetrics([&mainRows, &measuredSum, smoothedFrameTime, this](auto type, int typeIndex, float drawCalls, float frameTime, float percent, float costPerCall) {
		bool enabled = globals::state->enabledClasses[typeIndex - 1];
		std::optional<float> testFrameTime, testCostPerCall;
		auto it = this->testData.find(typeIndex);
		if (it != this->testData.end()) {
			testFrameTime = it->second.frameTime;
			testCostPerCall = it->second.costPerCall;
		}
		std::string label = std::string(magic_enum::enum_name(type)) + ":";
		std::string tooltip = "Draw calls for this shader type.";
		auto tipIt = kShaderTypeTooltips.find(type);
		if (tipIt != kShaderTypeTooltips.end()) {
			tooltip = tipIt->second;
		}
		mainRows.push_back({ label, typeIndex, static_cast<int>(drawCalls), frameTime, percent, costPerCall, tooltip, enabled, testFrameTime, testCostPerCall });
		measuredSum += frameTime;
	});

	auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);
	if (std::abs(otherFrameTime) < 1e-4f)
		otherFrameTime = 0.0f;
	std::optional<float> otherTestFrameTime, otherTestCostPerCall, totalTestFrameTime, totalTestCostPerCall;
	auto itOther = this->testData.find(magic_enum::enum_integer(SpecialShaderType::Other));
	if (itOther != this->testData.end()) {
		otherTestFrameTime = itOther->second.frameTime;
		otherTestCostPerCall = itOther->second.costPerCall;
	}
	auto itTotal = this->testData.find(magic_enum::enum_integer(SpecialShaderType::Total));
	if (itTotal != this->testData.end()) {
		totalTestFrameTime = itTotal->second.frameTime;
		totalTestCostPerCall = itTotal->second.costPerCall;
	}
	DrawCallRow otherRow = {
		"Other:", magic_enum::enum_integer(SpecialShaderType::Other), kDrawCallsNotApplicable, otherFrameTime, otherPercent,
		0.0f,
		std::string("Frame time not attributed to any measured shader type. This includes UI, post-processing, engine work, and any GPU activity not directly measured by the overlay."),
		true, otherTestFrameTime, otherTestCostPerCall
	};
	// Always use the actual total frame time for live data
	float totalFrameTime = smoothedFrameTime;
	float totalPercent = 100.0f;  // Total is always 100% of total

	DrawCallRow totalRow = {
		"Total:", magic_enum::enum_integer(SpecialShaderType::Total), static_cast<int>(globals::state->GetTotalSmoothedDrawCalls()), totalFrameTime, totalPercent,
		totalCostPerCall,
		std::string("Total frame time."),
		true, totalTestFrameTime, totalTestCostPerCall
	};
	std::vector<DrawCallRow> summaryRows;
	summaryRows.push_back(otherRow);
	summaryRows.push_back(totalRow);
	return { mainRows, summaryRows };
}

/**
  * @brief Creates a table row handler for the draw calls table
  *
  * This function creates a lambda that handles rendering individual table rows,
  * including special handling for summary rows (Total, Other) and normal shader
  * type rows. It ensures proper tooltip display and click handling for each row type.
  *
  * @param columns The column configurations for the table
  * @param overlay Pointer to the performance overlay instance
  * @return Function that handles row rendering and interaction
  */
std::function<void(int, int, const DrawCallRow&)> PerformanceOverlay::CreateTableRowHandler(const std::vector<ColumnConfig>& columns)
{
	return [&columns, this](int rowIdx, int colIdx, const DrawCallRow& row) {
		(void)rowIdx;
		// Special handling for summary rows
		if ((row.label == "Total:" || row.label == "Other:") && colIdx == 0) {
			if (row.label == "Total:") {
				if (ImGui::Selectable(row.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
					HandleTotalRowToggle();
				}
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(row.tooltip.c_str());
						float _fps = row.frameTime > 0.0f ? 1000.0f / row.frameTime : 0.0f;
						ImGui::Text("FPS: %.2f", _fps);
					}
				}
			} else if (row.label == "Other:") {
				ImGui::TextUnformatted(row.label.c_str());
				if (!row.tooltip.empty() && ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(row.tooltip.c_str());
					}
				}
			}
		} else if (row.label == "Total:" || row.label == "Other:") {
			// No tooltip for summary rows in non-label columns
			columns[colIdx].cellRender(row, colIdx);
		} else {
			// Normal row: ensure tooltips never modify cell content
			columns[colIdx].cellRender(row, colIdx);
		}
	};
}
// ============================================================================
// EVENT HANDLING FUNCTIONS
// ============================================================================

/**
  * @brief Handles shader type toggle functionality
  *
  * This function processes user clicks on shader type rows in the performance table.
  * When a shader is disabled, it captures the current performance data as test data
  * for comparison. This allows users to see the performance impact of disabling
  * specific shader types.
  *
  * @param row The draw call row that was clicked
  * @param wasEnabled Whether the shader was enabled before the click
  */
void PerformanceOverlay::HandleShaderToggle(const DrawCallRow& row, bool wasEnabled)
{
	auto maybeType = magic_enum::enum_cast<RE::BSShader::Type>(row.shaderType);
	if (!maybeType.has_value()) {
		return;
	}

	auto classIndex = magic_enum::enum_integer(*maybeType) - 1;
	if (classIndex < 0 || classIndex >= magic_enum::enum_integer(RE::BSShader::Type::Total) - 1) {
		return;
	}

	bool isDisabling = wasEnabled;
	float prevFrameTime = row.frameTime;
	float prevCostPerCall = row.costPerCall;

	// Capture live data for Total and Other before toggling
	float smoothedFrameTime = static_cast<float>(this->state.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	globals::state->ForEachShaderTypeWithMetrics([&measuredSum]([[maybe_unused]] auto type, [[maybe_unused]] int typeIndex, [[maybe_unused]] float drawCalls, float frameTime, [[maybe_unused]] float percent, [[maybe_unused]] float costPerCall) {
		measuredSum += frameTime;
	});
	auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);

	// Toggle the shader
	globals::state->enabledClasses[classIndex] = !wasEnabled;

	if (isDisabling) {
		// Save the last live value before disabling
		this->UpdateShaderTestData(row.shaderType, prevFrameTime, prevCostPerCall);
		// Save Total and Other test data as well
		this->testData[magic_enum::enum_integer(SpecialShaderType::Total)] = { smoothedFrameTime, totalCostPerCall, 100.0f };
		this->testData[magic_enum::enum_integer(SpecialShaderType::Other)] = { otherFrameTime, 0.0f, otherPercent };
		this->testDataSource = TestDataSource::ManualShaderToggle;
		QueryPerformanceCounter(&this->testDataLastUpdated);
	}
}

/**
  * @brief Handles the Total row toggle functionality
  *
  * This function processes clicks on the Total row in the performance table.
  * It toggles all shader types on/off simultaneously, allowing users to quickly
  * enable or disable all shaders for performance testing.
  */
void PerformanceOverlay::HandleTotalRowToggle()
{
	bool anyDisabled = false;
	globals::state->ForEachShaderTypeWithIndex([&anyDisabled]([[maybe_unused]] auto type, int classIndex) {
		if (!globals::state->enabledClasses[classIndex]) {
			anyDisabled = true;
		}
	});
	globals::state->ForEachShaderTypeWithIndex([&anyDisabled]([[maybe_unused]] auto type, int classIndex) {
		globals::state->enabledClasses[classIndex] = anyDisabled;
	});

	// Update test data and timestamp for manual toggling (not just A/B test mode)
	auto* abTestingManager = ABTestingManager::GetSingleton();
	bool abTest = abTestingManager && abTestingManager->IsEnabled() && abTestingManager->IsUsingTestConfig();
	if (abTest) {
		this->UpdateAllShaderTestData();
	} else {
		// Manual toggle: update test data and timestamp
		float smoothedFrameTime = static_cast<float>(this->state.smoothFrameTimeMs);
		float measuredSum = 0.0f;
		globals::state->ForEachShaderTypeWithMetrics([&measuredSum]([[maybe_unused]] auto type, [[maybe_unused]] int typeIndex, [[maybe_unused]] float drawCalls, float frameTime, [[maybe_unused]] float percent, [[maybe_unused]] float costPerCall) {
			measuredSum += frameTime;
		});
		auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);
		this->testData[magic_enum::enum_integer(SpecialShaderType::Total)] = { smoothedFrameTime, totalCostPerCall, 100.0f };
		this->testData[magic_enum::enum_integer(SpecialShaderType::Other)] = { otherFrameTime, 0.0f, otherPercent };
		this->testDataSource = TestDataSource::ManualShaderToggle;
		QueryPerformanceCounter(&this->testDataLastUpdated);
	}
}
// ============================================================================
// TEST DATA MANAGEMENT FUNCTIONS
// ============================================================================

// Static test data state

// Implement static member functions
/**
  * @brief Updates test data for a specific shader type during manual shader toggling
  *
  * This function captures performance data for a shader type when it's manually disabled,
  * allowing users to compare performance with/without specific shaders enabled.
  *
  * @param shaderType The shader type index to update test data for
  * @param frameTime The frame time contribution of this shader type (ms)
  * @param costPerCall The cost per draw call for this shader type (ms/call)
  *
  * @note This function also updates the Total and Other summary rows to maintain
  *       consistency with the current performance state
  */
void PerformanceOverlay::UpdateShaderTestData(int shaderType, float frameTime, float costPerCall)
{
	UpdateShaderTestDataEntry(shaderType, frameTime, costPerCall);

	float smoothedFrameTime = static_cast<float>(this->state.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	for (const auto& [type, data] : testData) {
		if (type >= 0)
			measuredSum += data.frameTime;
	}

	auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);
	UpdateSummaryTestData(smoothedFrameTime, otherFrameTime, otherPercent, totalCostPerCall);

	testDataSource = TestDataSource::ManualShaderToggle;
	QueryPerformanceCounter(&testDataLastUpdated);
}

/**
  * @brief Updates test data for all shader types during A/B test Variant B execution
  *
  * This function captures comprehensive performance data for all shader types when
  * running in A/B test mode with Variant B (test config) active. It ensures that
  * all shader types, including Total and Other summary rows, have current test data
  * for accurate performance comparison.
  *
  * @note This function only captures data when A/B testing is enabled and using
  *       Variant B (test config). It does nothing in manual shader toggle mode.
  */
void PerformanceOverlay::UpdateAllShaderTestData()
{
	// Check if all shaders are disabled
	bool allDisabled = true;
	globals::state->ForEachShaderTypeWithIndex([&allDisabled]([[maybe_unused]] auto type, int classIndex) {
		if (globals::state->enabledClasses[classIndex]) {
			allDisabled = false;
		}
	});
	if (allDisabled) {
		testData.clear();
		testDataSource = TestDataSource::None;
		return;
	}

	// Only capture test data if we're in A/B test mode AND using Variant B (test config)
	auto* abTestingManager = ABTestingManager::GetSingleton();
	bool abTest = abTestingManager && abTestingManager->IsEnabled() && abTestingManager->IsUsingTestConfig();
	if (!abTest) {
		// If not in A/B test Variant B, don't capture test data
		return;
	}

	float smoothedFrameTime = static_cast<float>(this->state.smoothFrameTimeMs);
	float measuredSum = 0.0f;

	globals::state->ForEachShaderTypeWithMetrics([&measuredSum, smoothedFrameTime, this]([[maybe_unused]] auto type, int typeIndex, [[maybe_unused]] float drawCalls, float frameTime, float percent, float costPerCall) {
		this->UpdateShaderTestDataEntry(typeIndex, frameTime, costPerCall, percent);
		measuredSum += frameTime;
	});

	auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);
	UpdateSummaryTestData(smoothedFrameTime, otherFrameTime, otherPercent, totalCostPerCall);
	testDataSource = TestDataSource::ABTest_VariantB;
	QueryPerformanceCounter(&testDataLastUpdated);
}

std::string PerformanceOverlay::GetTestDataTooltip() const
{
	switch (testDataSource) {
	case TestDataSource::ABTest_VariantB:
		return std::string("Test data from Test (Variant B).\nLast updated: ") + Util::TimeAgoStringQPC(testDataLastUpdated, state.overlayTimingFrequency) + " ago.";
	case TestDataSource::ManualShaderToggle:
		return std::string("Test data from manual shader toggle.\nLast updated: ") + Util::TimeAgoStringQPC(testDataLastUpdated, state.overlayTimingFrequency) + " ago.";
	default:
		return "No test data available.";
	}
}

// --- TEST DATA CAPTURE LOGIC ---
// Test data is captured in two scenarios:
// 1. A/B Test Mode (Variant B): If abTestingEnabled && usingTestConfig, we continuously capture test data
//    for all shader types, "Other", and "Total" every frame. This allows live comparison between
//    Variant A (user config) and Variant B (test config).
// 2. Manual Shader Toggle: If any shader is disabled, we capture test data for the disabled shaders
//    (and summary rows) at the moment of disabling, and keep it until cleared. This allows users to
//    compare performance with/without specific shaders enabled.
// Test data is only cleared by the "Clear Test Data" button or if all shaders are disabled (rare edge case).
void PerformanceOverlay::CaptureTestData()
{
	auto* abTestingManager = ABTestingManager::GetSingleton();
	bool abTestActive = (abTestingManager && abTestingManager->IsEnabled() && abTestingManager->IsUsingTestConfig());
	bool anyShaderDisabled = false;
	globals::state->ForEachShaderTypeWithIndex([&anyShaderDisabled]([[maybe_unused]] auto type, int classIndex) {
		if (!globals::state->enabledClasses[classIndex]) {
			anyShaderDisabled = true;
		}
	});
	float smoothedFrameTime = static_cast<float>(this->state.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	if (abTestActive) {
		measuredSum = 0.0f;
		globals::state->ForEachShaderTypeWithMetrics([&measuredSum, smoothedFrameTime, this]([[maybe_unused]] auto type, int typeIndex, [[maybe_unused]] float drawCalls, float frameTime, float percent, float costPerCall) {
			this->UpdateShaderTestDataEntry(typeIndex, frameTime, costPerCall, percent);
			measuredSum += frameTime;
		});
		auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);
		UpdateSummaryTestData(smoothedFrameTime, otherFrameTime, otherPercent, totalCostPerCall);
		testDataSource = TestDataSource::ABTest_VariantB;
		QueryPerformanceCounter(&testDataLastUpdated);
	} else if (anyShaderDisabled) {
		measuredSum = 0.0f;
		globals::state->ForEachShaderTypeWithMetrics([&measuredSum, smoothedFrameTime, this]([[maybe_unused]] auto type, int typeIndex, [[maybe_unused]] float drawCalls, float frameTime, float percent, float costPerCall) {
			bool enabled = globals::state->enabledClasses[typeIndex - 1];
			if (!enabled) {
				this->UpdateShaderTestDataEntry(typeIndex, frameTime, costPerCall, percent);
			}
			measuredSum += frameTime;
		});
		auto [otherFrameTime, otherPercent, totalCostPerCall] = CalculateSummaryData(smoothedFrameTime, measuredSum);
		UpdateSummaryTestData(smoothedFrameTime, otherFrameTime, otherPercent, totalCostPerCall);
		testDataSource = TestDataSource::ManualShaderToggle;
		QueryPerformanceCounter(&testDataLastUpdated);
	}
}

void PerformanceOverlay::ClearTestData()
{
	testData.clear();
	testDataSource = TestDataSource::None;
}

// Static helper method implementations
void PerformanceOverlay::UpdateShaderTestDataEntry(int shaderType, float frameTime, float costPerCall, float percent)
{
	testData[shaderType] = { frameTime, costPerCall, percent };
}

void PerformanceOverlay::UpdateSummaryTestData(float smoothedFrameTime, float otherFrameTime, float otherPercent, float totalCostPerCall)
{
	testData[magic_enum::enum_integer(SpecialShaderType::Other)] = { otherFrameTime, 0.0f, otherPercent };
	testData[magic_enum::enum_integer(SpecialShaderType::Total)] = { smoothedFrameTime, totalCostPerCall, 100.0f };
}
// ============================================================================
// PERFORMANCE OVERLAY STATE MANAGEMENT
// ============================================================================

void PerformanceOverlay::UpdateGraphValues()
{
	// Check if Frame Generation is active
	state.isFrameGenerationActive = globals::features::upscaling.IsFrameGenerationActive();

	// Sync frame history buffer size with user settings
	settings.FrameHistorySize = std::clamp(
		settings.FrameHistorySize,
		settings.kMinFrameHistorySize,
		settings.kMaxFrameHistorySize);
	state.frameTimeHistory.Resize(settings.FrameHistorySize);
	state.postFGFrameTimeHistory.Resize(settings.FrameHistorySize);

	// Calculate counter deltas
	REX::W32::QueryPerformanceCounter(&state.currentFrameCounter);
	int64_t elapsedCounter = state.currentFrameCounter - state.lastFrameCounter;
	state.lastFrameCounter = state.currentFrameCounter;

	// Calculate frametime and fps
	state.frameTimeMs = Util::CalcFrameTime(elapsedCounter, state.frequency);
	state.fps = Util::CalcFPS(state.frameTimeMs);

	// Calculate smooth values for display using the user-defined update interval
	// Initialize overlay timing frequency if needed
	if (state.overlayTimingFrequency.QuadPart == 0) {
		QueryPerformanceFrequency(&state.overlayTimingFrequency);
		QueryPerformanceCounter(&state.lastUpdateTime);
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float deltaTime = (now.QuadPart - state.lastUpdateTime.QuadPart) /
	                  static_cast<float>(state.overlayTimingFrequency.QuadPart);
	state.lastUpdateTime = now;

	// Insert latest frame time into circular buffer
	float oldFrameTime = state.frameTimeHistory.GetData()[state.frameTimeHistory.GetHeadIdx()];  // what is the point of oldFrameTime?
	state.frameTimeHistory.Push(state.frameTimeMs);

	// Maintain instantaneous min/max tracking
	if (state.frameTimeMs > state.maxFrameTime) {
		state.maxFrameTime = state.frameTimeMs;
	} else if (state.frameTimeMs < state.minFrameTime) {
		state.minFrameTime = state.frameTimeMs;
	} else if (oldFrameTime == state.minFrameTime) {
		state.minFrameTime = *std::ranges::min_element(state.frameTimeHistory.GetData());
	} else if (oldFrameTime == state.maxFrameTime) {
		state.maxFrameTime = *std::ranges::max_element(state.frameTimeHistory.GetData());
	}

	float avgFrameTime = kDefaultFrameTimeMs,
		  stdDev = 0.0f,
		  graphMin = 0.0f,
		  graphMax = Settings::kGraphSpreadMultiplier * kDefaultFrameTimeMs;
	// Calculate mean and standard deviation for normalized graph range
	if (!state.frameTimeHistory.GetData().empty()) {
		// Calculate average frame time
		avgFrameTime = std::accumulate(state.frameTimeHistory.GetData().begin(), state.frameTimeHistory.GetData().end(), 0.0f) / state.frameTimeHistory.GetData().size();

		// Calculate standard deviation
		float variance = 0.0f;
		for (float ft : state.frameTimeHistory.GetData()) {
			float diff = ft - avgFrameTime;
			variance += diff * diff;
		}
		variance /= state.frameTimeHistory.GetData().size();
		stdDev = std::sqrt(variance);

		// Calculate graph range
		float spread = std::clamp(stdDev * Settings::kGraphSpreadMultiplier, Settings::kGraphMinSpread, Settings::kGraphMaxSpread);
		graphMin = std::max(0.0f, avgFrameTime - spread);
		graphMax = avgFrameTime + spread;
	}

	// Exponential smoothing for stable graph scaling
	state.smoothedMinFrameTime = state.smoothedMinFrameTime + Settings::kSmoothingFactor * (graphMin - state.smoothedMinFrameTime);
	state.smoothedMaxFrameTime = state.smoothedMaxFrameTime + Settings::kSmoothingFactor * (graphMax - state.smoothedMaxFrameTime);

	if (state.isFrameGenerationActive) {
		// Get frametime directly from the Frame Generation system
		float fgDeltaTime = globals::features::upscaling.GetFrameGenerationFrameTime();

		// Check if FSR frame generation is active (FSR doesn't provide timing data)
		bool isFrameGenActive = globals::features::upscaling.IsFrameGenerationActive();
		if (fgDeltaTime > 0.0f && !isFrameGenActive) {
			state.postFGFrameTimeMs = fgDeltaTime * 1000.0f;
			state.postFGFps = 1000.0f / state.postFGFrameTimeMs;
		} else {
			// Fallback if FG time is not available
			state.postFGFrameTimeMs = state.frameTimeMs / Settings::kFrameGenerationMultiplier;
			state.postFGFps = state.fps * Settings::kFrameGenerationMultiplier;
		}

		// Update post-FG smooth values when timer elapses
		if (state.updateTimer <= 0.0f) {
			state.postFGSmoothFps = state.postFGFps;
			state.postFGSmoothFrameTimeMs = state.postFGFrameTimeMs;
		}

		// Update post-FG frametime history
		state.postFGFrameTimeHistory.Push(state.postFGFrameTimeMs);
	}

	// Update smooth values with user-specified interval
	state.updateTimer += deltaTime;
	if (state.updateTimer >= settings.UpdateInterval) {
		state.smoothFps = state.fps;  // Sampling white noise won't give you smoothed noise. This is useless.
		state.smoothFrameTimeMs = state.frameTimeMs;
		state.updateTimer = 0.0f;
	}
}
