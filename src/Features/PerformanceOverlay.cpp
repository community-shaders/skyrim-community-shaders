#include "PerformanceOverlay.h"
#include "Feature.h"
#include "FidelityFX.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Upscaling.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <magic_enum.hpp>
#include <numeric>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PerformanceOverlay::PerfOverlaySettings,
	ShowInOverlay,
	ShowDrawCalls,
	ShowVRAM,
	ShowFPS,
	ShowPreFGFrameTimeGraph,
	ShowPostFGFrameTimeGraph,
	UpdateInterval,
	FrameHistorySize,
	Size,
	BackgroundOpacity,
	ShowBorder,
	Position,
	PositionSet)

// Static test data state
// Remove global static variables and functions for test data
// Add static member variable definitions
PerformanceOverlay::TestDataSource PerformanceOverlay::s_testDataSource = PerformanceOverlay::TestDataSource::None;
std::chrono::steady_clock::time_point PerformanceOverlay::s_testDataLastUpdated;
std::unordered_map<int, PerformanceOverlay::TestData> PerformanceOverlay::s_testData;

// Implement static member functions
void PerformanceOverlay::UpdateShaderTestData(int shaderType, float frameTime, float costPerCall)
{
	s_testData[shaderType] = { frameTime, costPerCall };
	float smoothedFrameTime = static_cast<float>(GetSingleton()->perfOverlayState.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	for (const auto& [type, data] : s_testData) {
		if (type >= 0)
			measuredSum += data.frameTime;
	}
	float otherFrameTime = smoothedFrameTime - measuredSum;
	float otherPercent = (smoothedFrameTime > 0.0f) ? (otherFrameTime / smoothedFrameTime) * 100.0f : 0.0f;
	s_testData[-2] = { otherFrameTime, 0.0f, otherPercent };
	float totalSmoothedDrawCalls = static_cast<float>(globals::state->smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
	float totalCostPerCall = (totalSmoothedDrawCalls > 0.0f) ? (smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f;
	s_testData[-1] = { smoothedFrameTime, totalCostPerCall, 100.0f };

	s_testDataSource = TestDataSource::ManualShaderToggle;
	s_testDataLastUpdated = std::chrono::steady_clock::now();
}

void PerformanceOverlay::UpdateAllShaderTestData()
{
	if (!(Menu::GetSingleton() && Menu::GetSingleton()->abTestingEnabled && Menu::GetSingleton()->usingTestConfig))
		return;
	float smoothedFrameTime = static_cast<float>(GetSingleton()->perfOverlayState.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	float totalSmoothedDrawCalls = static_cast<float>(globals::state->smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
	for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
		if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
			continue;
		int typeIndex = magic_enum::enum_integer(type);
		float drawCalls = static_cast<float>(globals::state->smoothDrawCalls[typeIndex]);
		float frameTime = static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
		float percent = (smoothedFrameTime > 0.0f) ? (frameTime / smoothedFrameTime) * 100.0f : 0.0f;
		float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
		s_testData[typeIndex] = { frameTime, costPerCall, percent };
		measuredSum += frameTime;
	}
	float otherFrameTime = smoothedFrameTime - measuredSum;
	float otherPercent = (smoothedFrameTime > 0.0f) ? (otherFrameTime / smoothedFrameTime) * 100.0f : 0.0f;
	s_testData[-2] = { otherFrameTime, 0.0f, otherPercent };
	float totalCostPerCall = (totalSmoothedDrawCalls > 0.0f) ? (smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f;
	s_testData[-1] = { smoothedFrameTime, totalCostPerCall, 100.0f };
	s_testDataSource = TestDataSource::ABTest_VariantB;
	s_testDataLastUpdated = std::chrono::steady_clock::now();
}

std::string PerformanceOverlay::GetTestDataTooltip()
{
	switch (s_testDataSource) {
	case TestDataSource::ABTest_VariantB:
		return std::string("Test data from Test (Variant B).\nLast updated: ") + Util::TimeAgoString(s_testDataLastUpdated) + " ago.";
	case TestDataSource::ManualShaderToggle:
		return std::string("Test data from manual shader toggle.\nLast updated: ") + Util::TimeAgoString(s_testDataLastUpdated) + " ago.";
	default:
		return "No test data available.";
	}
}

void PerformanceOverlay::DataLoaded()
{
	// Initialize performance overlay state
	this->perfOverlayState.initialized = false;
	this->perfOverlayState.frameTimeHistory.resize(this->settings.FrameHistorySize, 0.0f);
	this->perfOverlayState.postFGFrameTimeHistory.resize(this->settings.FrameHistorySize, 0.0f);
}

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
		ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", Menu::KeyIdToString(menuSettings.OverlayToggleKey));
	}

	if (this->settings.ShowInOverlay) {
		ImGui::Indent();

		// Display options
		if (ImGui::CollapsingHeader("Display Options", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();

			ImGui::Checkbox("Show FPS Counter", &this->settings.ShowFPS);
			ImGui::Checkbox("Show Draw Calls", &this->settings.ShowDrawCalls);
			ImGui::Checkbox("Show VRAM Usage", &this->settings.ShowVRAM);

			bool isFrameGenerationActive = globals::upscaling && globals::upscaling->IsFrameGenerationActive();
			if (this->settings.ShowFPS && isFrameGenerationActive) {
				ImGui::Checkbox("Show Pre-FG Frametime Graph", &this->settings.ShowPreFGFrameTimeGraph);

				bool isFSRFrameGen = globals::fidelityFX && globals::fidelityFX->isFrameGenActive;
				if (isFSRFrameGen) {
					ImGui::BeginDisabled();
					ImGui::Checkbox("Show Post-FG Frametime Graph", &this->settings.ShowPostFGFrameTimeGraph);
					ImGui::EndDisabled();
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Post-FG timing not available with AMD FSR Frame Generation.\nThis option is only available with NVIDIA DLSS Frame Generation.");
					}
				} else {
					ImGui::Checkbox("Show Post-FG Frametime Graph", &this->settings.ShowPostFGFrameTimeGraph);
				}
			} else if (this->settings.ShowFPS) {
				ImGui::Checkbox("Show Frametime Graph", &this->settings.ShowPreFGFrameTimeGraph);
			}

			ImGui::Unindent();
		}

		// Appearance settings
		if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();

			const char* sizes[] = { "Small", "Medium", "Large" };
			int currentSize = static_cast<int>(this->settings.Size);
			if (ImGui::Combo("Text Size", &currentSize, sizes, IM_ARRAYSIZE(sizes))) {
				this->settings.Size = static_cast<PerfOverlaySettings::TextSize>(currentSize);
			}

			ImGui::SliderFloat("Background Opacity", &this->settings.BackgroundOpacity, 0.0f, 1.0f, "%.2f");
			ImGui::Checkbox("Show Border", &this->settings.ShowBorder);
			ImGui::SliderFloat("Update Interval", &this->settings.UpdateInterval, 0.001f, 2.0f, "%.2f seconds");
			ImGui::SliderInt("Frame History Size", &this->settings.FrameHistorySize,
				this->settings.kMinFrameHistorySize, this->settings.kMaxFrameHistorySize);

			ImGui::Separator();
			ImGui::Text("Position:");
			if (ImGui::Button("Reset Position")) {
				this->settings.PositionSet = false;
			}

			ImGui::Unindent();
		}
		ImGui::Unindent();
	}
}

void PerformanceOverlay::DrawOverlay()
{
	// Defensive: Check for required global state and ImGui context
	if (!globals::state || !Menu::GetSingleton()) {
		return;
	}
	// Check global overlay visibility
	if (!Menu::GetSingleton()->overlayVisible) {
		return;
	}
	// Defensive: Check for upscaling if you use it
	// (Not strictly needed here, but safe for future use)
	if (this->settings.ShowVRAM && (!Menu::GetSingleton()->GetDXGIAdapter3())) {
		return;
	}
	// Defensive: Check ImGui context
	if (!ImGui::GetCurrentContext()) {
		return;
	}
	if (!this->settings.ShowInOverlay) {
		return;
	}

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!PerformanceOverlay::GetSingleton()->settings.ShowInOverlay) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	if (!this->settings.ShowBorder) {
		windowFlags |= ImGuiWindowFlags_NoBackground;
	} else {
		windowFlags &= ~ImGuiWindowFlags_NoDecoration;
		windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
	}

	// Set background opacity
	ImGui::PushStyleColor(ImGuiCol_WindowBg,
		ImVec4(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x,
			ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).y,
			ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).z,
			this->settings.BackgroundOpacity));

	// Set text size based on user preference
	this->perfOverlayState.textScale = this->perfOverlayState.SetTextScale();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, this->settings.ShowBorder ? 1.0f : 0.0f);

	// Set initial position if not already set
	if (!this->settings.PositionSet) {
		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
		this->settings.Position = ImVec2(10.0f, 10.0f);
		this->settings.PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(this->settings.Position, ImGuiCond_FirstUseEver);
	}

	// Set window size based on whether graphs are shown, was rapidly changing size based on text
	this->perfOverlayState.hasGraphs = this->settings.ShowPreFGFrameTimeGraph ||
	                                   (this->settings.ShowPostFGFrameTimeGraph && this->perfOverlayState.isFrameGenerationActive);
	if (!this->perfOverlayState.hasGraphs) {
		float fixedWidth = 325.0f * this->perfOverlayState.textScale;
		ImGui::SetNextWindowSize(ImVec2(fixedWidth, 0), ImGuiCond_Always);
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

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));  // Tighter spacing
	ImGui::SetWindowFontScale(this->perfOverlayState.textScale);

	// Initialize Performance Counter if necessary
	if (!this->perfOverlayState.initialized) {
		REX::W32::QueryPerformanceFrequency(&this->perfOverlayState.frequency);
		REX::W32::QueryPerformanceCounter(&this->perfOverlayState.lastFrameCounter);
		this->perfOverlayState.initialized = true;
	} else {
		REX::W32::QueryPerformanceCounter(&this->perfOverlayState.currentFrameCounter);
		int64_t elapsedCounter = this->perfOverlayState.currentFrameCounter - this->perfOverlayState.lastFrameCounter;
		this->perfOverlayState.lastFrameCounter = this->perfOverlayState.currentFrameCounter;

		// Calculate frametime and fps
		this->perfOverlayState.frameTimeMs = Util::performanceOverlay.CalcFrameTime(elapsedCounter, this->perfOverlayState.frequency);
		this->perfOverlayState.fps = Util::performanceOverlay.CalcFPS(this->perfOverlayState.frameTimeMs);

		// Calculate smooth values for display using the user-defined update interval
		auto now = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(now - this->perfOverlayState.lastUpdateTime).count();
		this->perfOverlayState.lastUpdateTime = now;

		// Update graph values
		this->perfOverlayState.UpdateGraphValues();

		// Update smooth values with user-specified interval
		this->perfOverlayState.updateTimer += deltaTime;
		if (this->perfOverlayState.updateTimer >= this->settings.UpdateInterval) {
			this->perfOverlayState.smoothFps = this->perfOverlayState.fps;
			this->perfOverlayState.smoothFrameTimeMs = this->perfOverlayState.frameTimeMs;
			this->perfOverlayState.updateTimer = 0.0f;
		}

		// Check if Frame Generation is active
		this->perfOverlayState.isFrameGenerationActive = globals::upscaling && globals::upscaling->IsFrameGenerationActive();

		if (this->perfOverlayState.isFrameGenerationActive) {
			this->perfOverlayState.UpdateFGFrameTime();
		}

		// Show FPS counter if enabled
		if (this->settings.ShowFPS) {
			DrawFPS();
		}
	}

	// Show Draw Calls if enabled
	if (this->settings.ShowDrawCalls) {
		ImGui::Separator();
		DrawDrawCalls();
		ImGui::Separator();
	}

	// VRAM & GPU Usage
	if (this->settings.ShowVRAM && Menu::GetSingleton()->GetDXGIAdapter3()) {
		DrawVRAM();
	}

	ImGui::PopStyleVar();             // ItemSpacing
	ImGui::SetWindowFontScale(1.0f);  // Reset font scale

	ImGui::End();
	ImGui::PopStyleVar();    // WindowBorderSize
	ImGui::PopStyleColor();  // WindowBg
}

void PerformanceOverlay::PerfOverlayState::UpdateFGFrameTime()
{
	// Defensive: Check for upscaling pointer
	if (!globals::upscaling)
		return;

	// Get frametime directly from the Frame Generation system
	float fgDeltaTime = globals::upscaling->GetFrameGenerationFrameTime();
	if (fgDeltaTime > 0.0f) {
		GetSingleton()->perfOverlayState.postFGFrameTimeMs = fgDeltaTime * 1000.0f;
		GetSingleton()->perfOverlayState.postFGFps = 1000.0f / GetSingleton()->perfOverlayState.postFGFrameTimeMs;

		// Update post-FG smooth values when timer elapses
		if (GetSingleton()->perfOverlayState.updateTimer <= 0.0f) {
			GetSingleton()->perfOverlayState.postFGSmoothFps = GetSingleton()->perfOverlayState.postFGFps;
			GetSingleton()->perfOverlayState.postFGSmoothFrameTimeMs = GetSingleton()->perfOverlayState.postFGFrameTimeMs;
		}

		// Update post-FG frametime history
		GetSingleton()->perfOverlayState.postFGFrameTimeHistory[GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex] = GetSingleton()->perfOverlayState.postFGFrameTimeMs;
		GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex = (GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex + 1) % GetSingleton()->settings.FrameHistorySize;
	} else {
		// Fallback if FG time is not available
		GetSingleton()->perfOverlayState.postFGFrameTimeMs = GetSingleton()->perfOverlayState.frameTimeMs / 2.0f;
		GetSingleton()->perfOverlayState.postFGFps = GetSingleton()->perfOverlayState.fps * 2.0f;

		if (GetSingleton()->perfOverlayState.updateTimer <= 0.0f) {
			GetSingleton()->perfOverlayState.postFGSmoothFps = GetSingleton()->perfOverlayState.postFGFps;
			GetSingleton()->perfOverlayState.postFGSmoothFrameTimeMs = GetSingleton()->perfOverlayState.postFGFrameTimeMs;
		}

		GetSingleton()->perfOverlayState.postFGFrameTimeHistory[GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex] = GetSingleton()->perfOverlayState.postFGFrameTimeMs;
		GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex = (GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex + 1) % GetSingleton()->settings.FrameHistorySize;
	}
}

void PerformanceOverlay::PerfOverlayState::UpdateFrameTimeHistorySizes()
{
	GetSingleton()->settings.FrameHistorySize = std::clamp(
		GetSingleton()->settings.FrameHistorySize,
		GetSingleton()->settings.kMinFrameHistorySize,
		GetSingleton()->settings.kMaxFrameHistorySize);

	if (GetSingleton()->perfOverlayState.frameTimeHistory.size() != static_cast<size_t>(GetSingleton()->settings.FrameHistorySize)) {
		GetSingleton()->perfOverlayState.frameTimeHistory.resize(GetSingleton()->settings.FrameHistorySize, 0.0f);
		if (GetSingleton()->perfOverlayState.frameTimeHistoryIndex >= GetSingleton()->settings.FrameHistorySize) {
			GetSingleton()->perfOverlayState.frameTimeHistoryIndex = 0;
		}
	}
	if (GetSingleton()->perfOverlayState.postFGFrameTimeHistory.size() != static_cast<size_t>(GetSingleton()->settings.FrameHistorySize)) {
		GetSingleton()->perfOverlayState.postFGFrameTimeHistory.resize(GetSingleton()->settings.FrameHistorySize, 0.0f);
		if (GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex >= GetSingleton()->settings.FrameHistorySize) {
			GetSingleton()->perfOverlayState.postFGFrameTimeHistoryIndex = 0;
		}
	}
}

void PerformanceOverlay::PerfOverlayState::UpdateMinFrameTime()
{
	GetSingleton()->perfOverlayState.minFrameTime = *std::min_element(GetSingleton()->perfOverlayState.frameTimeHistory.begin(), GetSingleton()->perfOverlayState.frameTimeHistory.end());
}

void PerformanceOverlay::PerfOverlayState::UpdateMaxFrameTime()
{
	GetSingleton()->perfOverlayState.maxFrameTime = *std::max_element(GetSingleton()->perfOverlayState.frameTimeHistory.begin(), GetSingleton()->perfOverlayState.frameTimeHistory.end());
}

float PerformanceOverlay::PerfOverlayState::SetTextScale()
{
	switch (GetSingleton()->settings.Size) {
	case PerfOverlaySettings::TextSize::Small:
		return 0.8f;
	case PerfOverlaySettings::TextSize::Medium:
		return 1.0f;
	case PerfOverlaySettings::TextSize::Large:
		return 1.2f;
	}
	return 1.0f;
}

void PerformanceOverlay::DrawFPS()
{
	if (ImGui::BeginTable("FrametimeTargets", 2, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("##prop", ImGuiTableColumnFlags_WidthFixed, ImGui::GetTextLineHeight() * 5);
		ImGui::TableSetupColumn("##value");

		ImGui::TableNextColumn();
		ImGui::Text(this->perfOverlayState.isFrameGenerationActive ? "Raw FPS:" : "FPS:");
		ImGui::TableNextColumn();
		ImGui::Text("%.1f (%.2f ms)", this->perfOverlayState.smoothFps, this->perfOverlayState.smoothFrameTimeMs);

		if (this->perfOverlayState.isFrameGenerationActive) {
			ImGui::TableNextColumn();
			ImGui::Text("Post-FG FPS:");
			ImGui::TableNextColumn();
			ImGui::Text("%.1f (%.2f ms)", this->perfOverlayState.postFGSmoothFps, this->perfOverlayState.postFGSmoothFrameTimeMs);
		}

		ImGui::EndTable();
	}

	// Show Pre-FG frametime graph if enabled
	if (this->settings.ShowPreFGFrameTimeGraph) {
		// Prepare overlay text
		char overlay_text[128];
		snprintf(overlay_text, IM_ARRAYSIZE(overlay_text),
			"%s%.2f ms (%.1f FPS)",
			this->perfOverlayState.isFrameGenerationActive ? "Pre-FG: " : "",
			this->perfOverlayState.smoothFrameTimeMs, this->perfOverlayState.smoothFps);

		// Set graph colors
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));  // Green line

		// Draw the graph
		float graphWidth = ImGui::GetWindowWidth() * 0.9f;
		ImGui::PlotLines("##frametime",
			this->perfOverlayState.frameTimeHistory.data(),
			this->settings.FrameHistorySize,
			this->perfOverlayState.frameTimeHistoryIndex,
			overlay_text,
			this->perfOverlayState.smoothedMinFrameTime, this->perfOverlayState.smoothedMaxFrameTime,
			ImVec2(graphWidth, 50.0f * this->perfOverlayState.textScale));

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
	if (this->settings.ShowPostFGFrameTimeGraph && this->perfOverlayState.isFrameGenerationActive) {
		// Check if FSR frame generation is active (FSR doesn't provide timing data)
		bool isFSRFrameGen = globals::fidelityFX && globals::fidelityFX->isFrameGenActive;

		if (isFSRFrameGen) {
			// Show note that post-FG timing isn't available with FSR
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Post-FG timing not available with FSR3 Framegen");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("AMD FSR Frame Generation doesn't provide internal timing data.\nPost-FG performance metrics are only available with NVIDIA DLSS Frame Generation.");
			}
		} else {
			// Show post-FG graph for DLSS
			this->perfOverlayState.DrawPostFGFrameTimeGraph();
		}
	}
}

void PerformanceOverlay::PerfOverlayState::DrawPostFGFrameTimeGraph()
{
	// Prepare overlay text
	char overlay_text[128];
	snprintf(overlay_text, IM_ARRAYSIZE(overlay_text),
		"Post-FG: %.2f ms (%.1f FPS)",
		postFGSmoothFrameTimeMs, postFGSmoothFps);

	// Set graph colors - blue for post-FG
	ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));  // Blue line

	// Draw the graph
	float graphWidth = ImGui::GetWindowWidth() * 0.9f;
	ImGui::PlotLines("##postfgframetime",
		postFGFrameTimeHistory.data(),
		PerformanceOverlay::GetSingleton()->settings.FrameHistorySize,
		postFGFrameTimeHistoryIndex,
		overlay_text,
		smoothedMinFrameTime, smoothedMaxFrameTime,
		ImVec2(graphWidth, 50.0f * textScale));

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

void PerformanceOverlay::DrawDrawCalls()
{
	static bool clearTestDataRequested = false;
	bool abTestActive = (Menu::GetSingleton() && Menu::GetSingleton()->abTestingEnabled && Menu::GetSingleton()->usingTestConfig);
	bool anyShaderDisabled = false;
	for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1; ++i) {
		if (!globals::state->enabledClasses[i]) {
			anyShaderDisabled = true;
			break;
		}
	}
	if (!s_testData.empty()) {
		if (ImGui::Button("Clear Test Data")) {
			clearTestDataRequested = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Test columns are shown only when test data is present.");
	}

	// Build headers and column count dynamically
	std::vector<std::string> headers = { "Shader Type", "Draw Calls", "Frame Time (%)", "Cost/Call" };
	bool anyTestData = !s_testData.empty();
	if (anyTestData) {
		headers.push_back("Test Frame Time (%)");
		headers.push_back("Test Cost/Call");
	}
	int columnCount = static_cast<int>(headers.size());

	// Tooltip map for shader types
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
	std::vector<ShaderRow> shaderTypes;
	for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
		if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
			continue;
		std::string label = std::string(magic_enum::enum_name(type)) + ":";
		auto it = kShaderTypeTooltips.find(type);
		std::string tooltip = (it != kShaderTypeTooltips.end()) ? it->second : "Draw calls for this shader type.";
		tooltip += "\nClick to ";
		tooltip += (globals::state->enabledClasses[magic_enum::enum_integer(type) - 1]) ? "disable" : "enable";
		tooltip += " this shader.";
		shaderTypes.push_back({ label, type, tooltip });
	}

	// Sorting state (declare all at the top)
	static int sortColumn = 0;
	static bool sortAscending = false;
	static bool isSorted = false;
	static std::vector<DrawCallRow> originalRows;

	// Use the smoothed frame time for all calculations
	float smoothedFrameTime = static_cast<float>(this->perfOverlayState.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	float totalSmoothedDrawCalls = static_cast<float>(globals::state->smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);

	// --- TEST DATA SNAPSHOT LOGIC ---
	if (abTestActive) {
		measuredSum = 0.0f;
		for (const auto& row : shaderTypes) {
			auto typeIndex = magic_enum::enum_integer(static_cast<RE::BSShader::Type>(row.type));
			float drawCalls = static_cast<float>(globals::state->smoothDrawCalls[typeIndex]);
			float frameTime = static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
			float percent = (smoothedFrameTime > 0.0f) ? (frameTime / smoothedFrameTime) * 100.0f : 0.0f;
			float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
			s_testData[typeIndex] = { frameTime, costPerCall, percent };
			measuredSum += frameTime;
		}
		float otherFrameTime = smoothedFrameTime - measuredSum;
		float otherPercent = (smoothedFrameTime > 0.0f) ? (otherFrameTime / smoothedFrameTime) * 100.0f : 0.0f;
		s_testData[-2] = { otherFrameTime, 0.0f, otherPercent };
		float totalCostPerCall = (totalSmoothedDrawCalls > 0.0f) ? (smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f;
		s_testData[-1] = { smoothedFrameTime, totalCostPerCall, 100.0f };
	} else if (anyShaderDisabled) {
		measuredSum = 0.0f;
		for (const auto& row : shaderTypes) {
			auto typeIndex = magic_enum::enum_integer(static_cast<RE::BSShader::Type>(row.type));
			bool enabled = globals::state->enabledClasses[typeIndex - 1];
			if (!enabled) {
				float drawCalls = static_cast<float>(globals::state->smoothDrawCalls[typeIndex]);
				float frameTime = static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
				float percent = (smoothedFrameTime > 0.0f) ? (frameTime / smoothedFrameTime) * 100.0f : 0.0f;
				float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
				s_testData[typeIndex] = { frameTime, costPerCall, percent };
			}
			measuredSum += static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
		}
		float otherFrameTime = smoothedFrameTime - measuredSum;
		float otherPercent = (smoothedFrameTime > 0.0f) ? (otherFrameTime / smoothedFrameTime) * 100.0f : 0.0f;
		s_testData[-2] = { otherFrameTime, 0.0f, otherPercent };
		float totalCostPerCall = (totalSmoothedDrawCalls > 0.0f) ? (smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f;
		s_testData[-1] = { smoothedFrameTime, totalCostPerCall, 100.0f };
	}

	// Build main rows: all shader types
	std::vector<DrawCallRow> mainRows;
	measuredSum = 0.0f;
	for (const auto& row : shaderTypes) {
		auto typeIndex = magic_enum::enum_integer(static_cast<RE::BSShader::Type>(row.type));
		float drawCalls = static_cast<float>(globals::state->smoothDrawCalls[typeIndex]);
		float frameTime = static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
		float percent = (smoothedFrameTime > 0.0f) ? (frameTime / smoothedFrameTime) * 100.0f : 0.0f;
		float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
		// Clamp small negative values to zero
		if (std::abs(frameTime) < 1e-4f)
			frameTime = 0.0f;
		if (std::abs(costPerCall) < 1e-6f)
			costPerCall = 0.0f;
		bool enabled = globals::state->enabledClasses[typeIndex - 1];
		std::optional<float> testFrameTime, testCostPerCall;
		auto it = s_testData.find(typeIndex);
		if (it != s_testData.end()) {
			testFrameTime = it->second.frameTime;
			testCostPerCall = it->second.costPerCall;
			anyTestData = true;
		}
		DrawCallRow rowObj{ row.label, typeIndex, static_cast<int>(drawCalls), frameTime, percent, costPerCall, row.tooltip, enabled, testFrameTime, testCostPerCall };
		mainRows.push_back(rowObj);
		measuredSum += frameTime;
	}

	// Add summary rows (not part of main sorting)
	float otherFrameTime = smoothedFrameTime - measuredSum;
	if (std::abs(otherFrameTime) < 1e-4f)
		otherFrameTime = 0.0f;

	DrawCallRow otherRow = {
		"Other:", -2, 0, otherFrameTime,
		(smoothedFrameTime > 0.0f ? static_cast<float>((otherFrameTime / smoothedFrameTime) * 100.0f) : 0.0f),
		0.0f,
		std::string("Frame time not attributed to any measured shader type. This includes UI, post-processing, engine work, and any GPU activity not directly measured by the overlay.")
	};

	DrawCallRow totalRow = {
		"Total:", -1, static_cast<int>(totalSmoothedDrawCalls), smoothedFrameTime, 100.0f,
		(totalSmoothedDrawCalls > 0.0f ? static_cast<float>(smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f),
		std::string("Sum of all measured shader types.\nClick to enable or disable all shaders.")
	};

	// Always start the main table sorted alphabetically by label on first display
	static bool firstTableDisplay = true;
	if (firstTableDisplay) {
		std::sort(mainRows.begin(), mainRows.end(), [](const DrawCallRow& a, const DrawCallRow& b) {
			return a.label < b.label;
		});
		firstTableDisplay = false;
	}

	// Handle sorting: only sort mainRows, never summary rows
	if (ImGui::BeginTable("DrawCallOverlayTable##", columnCount, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
		// --- HEADER SETUP WITH TOOLTIP ---
		for (int i = 0; i < headers.size(); ++i) {
			ImGui::TableSetupColumn(headers[i].c_str());
			if (anyTestData && (headers[i] == "Test Frame Time" || headers[i] == "Test Cost/Call")) {
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", GetTestDataTooltip().c_str());
				}
			}
		}
		ImGui::TableHeadersRow();

		if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
			if (sortSpecs->SpecsCount > 0) {
				sortColumn = sortSpecs->Specs->ColumnIndex;
				sortAscending = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
				switch (sortColumn) {
				case Col_Label:
					std::sort(mainRows.begin(), mainRows.end(), [=](const DrawCallRow& a, const DrawCallRow& b) {
						return sortAscending ? (a.label < b.label) : (a.label > b.label);
					});
					break;
				case Col_DrawCalls:
					std::sort(mainRows.begin(), mainRows.end(), [=](const DrawCallRow& a, const DrawCallRow& b) {
						return sortAscending ? (a.drawCalls < b.drawCalls) : (a.drawCalls > b.drawCalls);
					});
					break;
				case Col_FrameTime:
					std::sort(mainRows.begin(), mainRows.end(), [=](const DrawCallRow& a, const DrawCallRow& b) {
						return sortAscending ? (a.percent < b.percent) : (a.percent > b.percent);
					});
					break;
				case Col_CostPerCall:
					std::sort(mainRows.begin(), mainRows.end(), [=](const DrawCallRow& a, const DrawCallRow& b) {
						return sortAscending ? (a.costPerCall < b.costPerCall) : (a.costPerCall > b.costPerCall);
					});
					break;
				case Col_TestFrameTime:
					std::sort(mainRows.begin(), mainRows.end(), [=](const DrawCallRow& a, const DrawCallRow& b) {
						float aVal = a.testFrameTime.value_or(FLT_MAX);
						float bVal = b.testFrameTime.value_or(FLT_MAX);
						return sortAscending ? (aVal < bVal) : (aVal > bVal);
					});
					break;
				case Col_TestCostPerCall:
					std::sort(mainRows.begin(), mainRows.end(), [=](const DrawCallRow& a, const DrawCallRow& b) {
						float aVal = a.testCostPerCall.value_or(FLT_MAX);
						float bVal = b.testCostPerCall.value_or(FLT_MAX);
						return sortAscending ? (aVal < bVal) : (aVal > bVal);
					});
					break;
				}
			}
		}

		// --- Main table cell rendering switch: renders each cell based on column ---
		for (size_t rowIdx = 0; rowIdx < mainRows.size(); ++rowIdx) {
			const auto& row = mainRows[rowIdx];
			ImGui::TableNextRow();
			int colIdx = 0;
			// Label
			ImGui::TableSetColumnIndex(colIdx++);
			if (!row.enabled) {
				ImGui::PushStyleColor(ImGuiCol_Text, Menu::GetSingleton()->GetTheme().StatusPalette.Disable);
			}
			bool wasEnabled = row.enabled;
			if (ImGui::Selectable(row.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
				auto maybeType = magic_enum::enum_cast<RE::BSShader::Type>(row.shaderType);
				if (maybeType.has_value()) {
					auto classIndex = magic_enum::enum_integer(*maybeType) - 1;
					if (classIndex >= 0 && classIndex < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1) {
						globals::state->enabledClasses[classIndex] = !wasEnabled;
						// Also update test data when toggled
						UpdateShaderTestData(row.shaderType, row.frameTime, row.costPerCall);
					}
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
				ImGui::SetTooltip("%s", row.tooltip.c_str());
			}
			if (!row.enabled) {
				ImGui::PopStyleColor();
			}
			// Draw Calls
			ImGui::TableSetColumnIndex(colIdx++);
			ImGui::Text("%d", row.drawCalls);
			// Frame Time (%)
			ImGui::TableSetColumnIndex(colIdx++);
			{
				std::string frameTimeStr = Util::FormatMilliseconds(row.frameTime) + " (" + Util::FormatPercent(row.percent) + ")";
				ImVec4 color = Util::GetThresholdColor(row.percent, 30.0f, 60.0f,
					Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor,
					Menu::GetSingleton()->GetTheme().StatusPalette.Warning,
					Menu::GetSingleton()->GetTheme().StatusPalette.Error);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::Text("%s", frameTimeStr.c_str());
				ImGui::PopStyleColor();
			}
			// Cost/Call
			ImGui::TableSetColumnIndex(colIdx++);
			{
				ImVec4 color = Util::GetThresholdColor(row.costPerCall, 0.05f, 0.2f,
					Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor,
					Menu::GetSingleton()->GetTheme().StatusPalette.Warning,
					Menu::GetSingleton()->GetTheme().StatusPalette.Error);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				if (row.costPerCall < 0.01f && row.costPerCall > 0.0f)
					ImGui::Text("%s", Util::FormatMicroseconds(row.costPerCall * 1000.0f).c_str());
				else
					ImGui::Text("%s", Util::FormatMilliseconds(row.costPerCall).c_str());
				ImGui::PopStyleColor();
			}
			// Test columns if present
			if (anyTestData) {
				// Test Frame Time
				ImGui::TableSetColumnIndex(colIdx++);
				if (row.testFrameTime.has_value()) {
					ImVec4 testColor = Util::GetThresholdColor(*row.testFrameTime, row.frameTime, row.frameTime + 0.01f,
						Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor,
						Menu::GetSingleton()->GetTheme().StatusPalette.Warning,
						Menu::GetSingleton()->GetTheme().StatusPalette.Error);
					ImGui::PushStyleColor(ImGuiCol_Text, testColor);
					std::string testFrameTimeStr = Util::FormatMilliseconds(*row.testFrameTime) + " (" + Util::FormatPercent(s_testData[row.shaderType].percent) + ")";
					ImGui::Text("%s", testFrameTimeStr.c_str());
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", GetTestDataTooltip().c_str());
					}
				} else {
					ImGui::TextDisabled("-");
				}
				// Test Cost/Call
				ImGui::TableSetColumnIndex(colIdx++);
				if (row.testCostPerCall.has_value()) {
					ImVec4 testColor = Util::GetThresholdColor(*row.testCostPerCall, row.costPerCall, row.costPerCall + 0.01f,
						Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor,
						Menu::GetSingleton()->GetTheme().StatusPalette.Warning,
						Menu::GetSingleton()->GetTheme().StatusPalette.Error);
					ImGui::PushStyleColor(ImGuiCol_Text, testColor);
					if (*row.testCostPerCall < 0.01f && *row.testCostPerCall > 0.0f)
						ImGui::Text("%s", Util::FormatMicroseconds(*row.testCostPerCall * 1000.0f).c_str());
					else
						ImGui::Text("%s", Util::FormatMilliseconds(*row.testCostPerCall).c_str());
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", GetTestDataTooltip().c_str());
					}
				} else {
					ImGui::TextDisabled("-");
				}
			}
		}
		// Separator row (optional, for visual separation)
		ImGui::TableNextRow(ImGuiTableRowFlags_None, 4.0f);
		for (int col = 0; col < columnCount; ++col) {
			ImGui::TableSetColumnIndex(col);
			if (col == 0)
				ImGui::Separator();
		}
		// Summary rows (Other, Total)
		for (const DrawCallRow& row : { otherRow, totalRow }) {
			ImGui::TableNextRow();
			int colIdx = 0;
			ImGui::TableSetColumnIndex(colIdx++);
			if (row.label == std::string("Total:")) {
				if (ImGui::Selectable(row.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
					bool anyDisabled = false;
					for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1; ++i) {
						if (!globals::state->enabledClasses[i]) {
							anyDisabled = true;
							break;
						}
					}
					for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1; ++i) {
						globals::state->enabledClasses[i] = anyDisabled;
					}
				}
			} else {
				ImGui::TextUnformatted(row.label.c_str());
			}
			if (!row.tooltip.empty() && ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted(row.tooltip.c_str());
				}
			}
			ImGui::TableSetColumnIndex(colIdx++);
			if (row.label == "Other:" && row.drawCalls <= 0) {
				ImGui::TextUnformatted("N/A");
			} else {
				ImGui::Text("%d", row.drawCalls);
			}
			ImGui::TableSetColumnIndex(colIdx++);
			if (row.label == "Other:" && row.frameTime <= 0.0f) {
				ImGui::TextUnformatted("N/A");
			} else {
				ImGui::Text("%.2f ms (%.1f%%)", row.frameTime, row.percent);
				if (row.label == "Total:" && ImGui::IsItemHovered()) {
					float _fps = row.frameTime > 0.0f ? 1000.0f / row.frameTime : 0.0f;
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("FPS: %.2f", _fps);
					}
				}
			}
			ImGui::TableSetColumnIndex(colIdx++);
			if (row.label == "Other:" && row.costPerCall <= 0.0f) {
				ImGui::TextUnformatted("N/A");
			} else if (row.costPerCall < 0.01f && row.costPerCall > 0.0f) {
				ImGui::Text("%.2f us", row.costPerCall * 1000.0f);
			} else {
				ImGui::Text("%.3f ms", row.costPerCall);
			}
			if (anyTestData) {
				ImGui::TableSetColumnIndex(colIdx++);
				auto testIt = s_testData.find(row.shaderType);
				if (testIt != s_testData.end()) {
					ImVec4 testColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
					if (testIt->second.frameTime < row.frameTime)
						testColor = Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor;
					else if (testIt->second.frameTime > row.frameTime)
						testColor = Menu::GetSingleton()->GetTheme().StatusPalette.Error;
					ImGui::PushStyleColor(ImGuiCol_Text, testColor);
					ImGui::Text("%.2f ms (%.1f%%)", testIt->second.frameTime, testIt->second.percent);
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", GetTestDataTooltip().c_str());
						// Show FPS for test frame time
						float _fps = testIt->second.frameTime > 0.0f ? 1000.0f / testIt->second.frameTime : 0.0f;
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("FPS: %.2f", _fps);
						}
					}
				} else {
					ImGui::TextUnformatted("N/A");
				}
				ImGui::TableSetColumnIndex(colIdx++);
				if (testIt != s_testData.end()) {
					ImVec4 testColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
					if (testIt->second.costPerCall < row.costPerCall)
						testColor = Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor;
					else if (testIt->second.costPerCall > row.costPerCall)
						testColor = Menu::GetSingleton()->GetTheme().StatusPalette.Error;
					ImGui::PushStyleColor(ImGuiCol_Text, testColor);
					if (testIt->second.costPerCall < 0.01f && testIt->second.costPerCall > 0.0f)
						ImGui::Text("%.2f us", testIt->second.costPerCall * 1000.0f);
					else
						ImGui::Text("%.3f ms", testIt->second.costPerCall);
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", GetTestDataTooltip().c_str());
					}
				} else {
					ImGui::TextUnformatted("N/A");
				}
			}
		}
		ImGui::EndTable();
	}

	if (clearTestDataRequested) {
		s_testData.clear();
		clearTestDataRequested = false;
		s_testDataSource = TestDataSource::None;
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

ImVec4 PerformanceOverlay::GetPerformanceColor(float value, float lowThreshold, float highThreshold)
{
	auto menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();

	if (value <= lowThreshold) {
		return theme.StatusPalette.SuccessColor;
	} else if (value <= highThreshold) {
		return theme.StatusPalette.Warning;
	} else {
		return theme.StatusPalette.Error;
	}
}

ImVec4 PerformanceOverlay::GetPerformanceStatusColor(int status)
{
	auto menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();

	switch (status) {
	case 0:  // Good
		return theme.StatusPalette.SuccessColor;
	case 1:  // Warning
		return theme.StatusPalette.Warning;
	case 2:  // Error
		return theme.StatusPalette.Error;
	case 3:  // Info
		return theme.StatusPalette.InfoColor;
	default:
		return theme.StatusPalette.InfoColor;
	}
}

bool PerformanceOverlay::RenderColorCodedMetric(const char* label, float value, float lowThreshold, float highThreshold, const char* format)
{
	ImVec4 color = GetPerformanceColor(value, lowThreshold, highThreshold);
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::Text(label);
	ImGui::SameLine();
	ImGui::Text(format, value);
	ImGui::PopStyleColor();
	return ImGui::IsItemHovered();
}

/**
 * @brief Updates all runtime state related to the performance overlay graph.
 *
 * This function synchronizes the frame time history buffer, tracks min/max frame times,
 * and computes the normalized Y-axis range for the frame time graph using statistical analysis.
 *
 * Steps performed:
 *   1. Resizes the frameTimeHistory buffer if the user has changed the setting.
 *   2. Inserts the latest frame time into the circular history buffer.
 *   3. Updates instantaneous min/max frame time values, with full rescans if necessary.
 *   4. Calculates the average (mean) and standard deviation of frame times in the buffer.
 *   5. Sets the graph Y-axis range to be centered on the average, with a spread of ±2 standard deviations,
 *      clamped to user-friendly minimum and maximum values.
 *   6. Smooths the min/max Y-axis values for visual stability using exponential smoothing.
 *
 *
 * No parameters; uses settings from the singleton.
 */
void PerformanceOverlay::PerfOverlayState::UpdateGraphValues()
{
	// Get settings from the singleton
	const auto& overlaySettings = PerformanceOverlay::GetSingleton()->settings;

	// Sync frame history buffer size with user settings
	UpdateFrameTimeHistorySizes();

	// Insert latest frame time into circular buffer
	float oldFrameTime = frameTimeHistory[frameTimeHistoryIndex];
	frameTimeHistory[frameTimeHistoryIndex] = frameTimeMs;
	frameTimeHistoryIndex = (frameTimeHistoryIndex + 1) % overlaySettings.FrameHistorySize;

	// Maintain instantaneous min/max tracking
	if (frameTimeMs > maxFrameTime) {
		maxFrameTime = frameTimeMs;
	} else if (frameTimeMs < minFrameTime) {
		minFrameTime = frameTimeMs;
	} else if (oldFrameTime == minFrameTime) {
		UpdateMinFrameTime();
	} else if (oldFrameTime == maxFrameTime) {
		UpdateMaxFrameTime();
	}

	float avgFrameTime, stdDev, graphMin, graphMax;
	// Calculate mean and standard deviation for normalized graph range
	if (frameTimeHistory.empty()) {
		// Default to 60 FPS
		avgFrameTime = 16.67f;
		stdDev = 0.0f;
		graphMin = 0.0f;
		graphMax = 33.0f;
	} else {
		// Calculate average frame time
		avgFrameTime = std::accumulate(frameTimeHistory.begin(), frameTimeHistory.end(), 0.0f) / frameTimeHistory.size();

		// Calculate standard deviation
		float variance = 0.0f;
		for (float ft : frameTimeHistory) {
			float diff = ft - avgFrameTime;
			variance += diff * diff;
		}
		variance /= frameTimeHistory.size();
		stdDev = std::sqrt(variance);

		// Calculate graph range
		float spread = std::clamp(stdDev * 2.0f, 2.0f, 20.0f);
		graphMin = std::max(0.0f, avgFrameTime - spread);
		graphMax = avgFrameTime + spread;
	}

	// Exponential smoothing for stable graph scaling
	smoothedMinFrameTime += kSmoothingFactor * (graphMin - smoothedMinFrameTime);
	smoothedMaxFrameTime += kSmoothingFactor * (graphMax - smoothedMaxFrameTime);
}