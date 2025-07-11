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

// Static test data state
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
	// Check if all shaders are disabled
	bool allDisabled = true;
	for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1; ++i) {
		if (globals::state->enabledClasses[i]) {
			allDisabled = false;
			break;
		}
	}
	if (allDisabled) {
		s_testData.clear();
		s_testDataSource = TestDataSource::None;
		return;
	}

	// Only capture test data if we're in A/B test mode AND using Variant B (test config)
	bool abTest = Menu::GetSingleton() && Menu::GetSingleton()->abTestingEnabled && Menu::GetSingleton()->usingTestConfig;
	if (!abTest) {
		// If not in A/B test Variant B, don't capture test data
		return;
	}

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
	auto* menu = Menu::GetSingleton();

	// Defensive: Check for required global state and ImGui context
	if (!globals::state || !menu) {
		return;
	}
	// Check global overlay visibility
	if (!menu->overlayVisible) {
		return;
	}
	// Defensive: Check for upscaling if you use it
	// (Not strictly needed here, but safe for future use)
	if (this->settings.ShowVRAM && (!menu->GetDXGIAdapter3())) {
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
	if (!this->settings.ShowInOverlay) {
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
		this->perfOverlayState.frameTimeMs = Util::CalcFrameTime(elapsedCounter, this->perfOverlayState.frequency);
		this->perfOverlayState.fps = Util::CalcFPS(this->perfOverlayState.frameTimeMs);

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
	if (this->settings.ShowVRAM && menu->GetDXGIAdapter3()) {
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

	auto* overlay = GetSingleton();

	// Get frametime directly from the Frame Generation system
	float fgDeltaTime = globals::upscaling->GetFrameGenerationFrameTime();
	if (fgDeltaTime > 0.0f) {
		overlay->perfOverlayState.postFGFrameTimeMs = fgDeltaTime * 1000.0f;
		overlay->perfOverlayState.postFGFps = 1000.0f / overlay->perfOverlayState.postFGFrameTimeMs;

		// Update post-FG smooth values when timer elapses
		if (overlay->perfOverlayState.updateTimer <= 0.0f) {
			overlay->perfOverlayState.postFGSmoothFps = overlay->perfOverlayState.postFGFps;
			overlay->perfOverlayState.postFGSmoothFrameTimeMs = overlay->perfOverlayState.postFGFrameTimeMs;
		}

		// Update post-FG frametime history
		overlay->perfOverlayState.postFGFrameTimeHistory[overlay->perfOverlayState.postFGFrameTimeHistoryIndex] = overlay->perfOverlayState.postFGFrameTimeMs;
		overlay->perfOverlayState.postFGFrameTimeHistoryIndex = (overlay->perfOverlayState.postFGFrameTimeHistoryIndex + 1) % overlay->settings.FrameHistorySize;
	} else {
		// Fallback if FG time is not available
		overlay->perfOverlayState.postFGFrameTimeMs = overlay->perfOverlayState.frameTimeMs / 2.0f;
		overlay->perfOverlayState.postFGFps = overlay->perfOverlayState.fps * 2.0f;

		if (overlay->perfOverlayState.updateTimer <= 0.0f) {
			overlay->perfOverlayState.postFGSmoothFps = overlay->perfOverlayState.postFGFps;
			overlay->perfOverlayState.postFGSmoothFrameTimeMs = overlay->perfOverlayState.postFGFrameTimeMs;
		}

		overlay->perfOverlayState.postFGFrameTimeHistory[overlay->perfOverlayState.postFGFrameTimeHistoryIndex] = overlay->perfOverlayState.postFGFrameTimeMs;
		overlay->perfOverlayState.postFGFrameTimeHistoryIndex = (overlay->perfOverlayState.postFGFrameTimeHistoryIndex + 1) % overlay->settings.FrameHistorySize;
	}
}

void PerformanceOverlay::PerfOverlayState::UpdateFrameTimeHistorySizes()
{
	auto* overlay = GetSingleton();

	overlay->settings.FrameHistorySize = std::clamp(
		overlay->settings.FrameHistorySize,
		overlay->settings.kMinFrameHistorySize,
		overlay->settings.kMaxFrameHistorySize);

	if (overlay->perfOverlayState.frameTimeHistory.size() != static_cast<size_t>(overlay->settings.FrameHistorySize)) {
		overlay->perfOverlayState.frameTimeHistory.resize(overlay->settings.FrameHistorySize, 0.0f);
		if (overlay->perfOverlayState.frameTimeHistoryIndex >= overlay->settings.FrameHistorySize) {
			overlay->perfOverlayState.frameTimeHistoryIndex = 0;
		}
	}
	if (overlay->perfOverlayState.postFGFrameTimeHistory.size() != static_cast<size_t>(overlay->settings.FrameHistorySize)) {
		overlay->perfOverlayState.postFGFrameTimeHistory.resize(overlay->settings.FrameHistorySize, 0.0f);
		if (overlay->perfOverlayState.postFGFrameTimeHistoryIndex >= overlay->settings.FrameHistorySize) {
			overlay->perfOverlayState.postFGFrameTimeHistoryIndex = 0;
		}
	}
}

void PerformanceOverlay::PerfOverlayState::UpdateMinFrameTime()
{
	auto* overlay = GetSingleton();
	overlay->perfOverlayState.minFrameTime = *std::min_element(overlay->perfOverlayState.frameTimeHistory.begin(), overlay->perfOverlayState.frameTimeHistory.end());
}

void PerformanceOverlay::PerfOverlayState::UpdateMaxFrameTime()
{
	auto* overlay = GetSingleton();
	overlay->perfOverlayState.maxFrameTime = *std::max_element(overlay->perfOverlayState.frameTimeHistory.begin(), overlay->perfOverlayState.frameTimeHistory.end());
}

float PerformanceOverlay::PerfOverlayState::SetTextScale()
{
	auto* overlay = GetSingleton();
	switch (overlay->settings.Size) {
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
	auto* menu = Menu::GetSingleton();
	bool abTestActive = (menu && menu->abTestingEnabled && menu->usingTestConfig);
	bool anyShaderDisabled = false;
	for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1; ++i) {
		if (!globals::state->enabledClasses[i]) {
			anyShaderDisabled = true;
			break;
		}
	}
	float smoothedFrameTime = static_cast<float>(this->perfOverlayState.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	float totalSmoothedDrawCalls = static_cast<float>(globals::state->smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
	if (abTestActive) {
		measuredSum = 0.0f;
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
	} else if (anyShaderDisabled) {
		measuredSum = 0.0f;
		for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
			if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
				continue;
			int typeIndex = magic_enum::enum_integer(type);
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
		s_testDataSource = TestDataSource::ManualShaderToggle;
		s_testDataLastUpdated = std::chrono::steady_clock::now();
	}
}

void PerformanceOverlay::ClearTestData()
{
	s_testData.clear();
	s_testDataSource = TestDataSource::None;
}

std::pair<std::vector<DrawCallRow>, std::vector<DrawCallRow>> PerformanceOverlay::BuildDrawCallRows() const
{
	std::vector<DrawCallRow> mainRows;
	float smoothedFrameTime = static_cast<float>(this->perfOverlayState.smoothFrameTimeMs);
	float measuredSum = 0.0f;
	for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
		if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
			continue;
		int typeIndex = magic_enum::enum_integer(type);
		float drawCalls = static_cast<float>(globals::state->smoothDrawCalls[typeIndex]);
		float frameTime = static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
		float percent = (smoothedFrameTime > 0.0f) ? (frameTime / smoothedFrameTime) * 100.0f : 0.0f;
		float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
		bool enabled = globals::state->enabledClasses[typeIndex - 1];
		std::optional<float> testFrameTime, testCostPerCall;
		auto it = s_testData.find(typeIndex);
		if (it != s_testData.end()) {
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
	}
	float totalSmoothedDrawCalls = static_cast<float>(globals::state->smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
	float otherFrameTime = smoothedFrameTime - measuredSum;
	if (std::abs(otherFrameTime) < 1e-4f)
		otherFrameTime = 0.0f;
	std::optional<float> otherTestFrameTime, otherTestCostPerCall, totalTestFrameTime, totalTestCostPerCall;
	auto itOther = s_testData.find(-2);
	if (itOther != s_testData.end()) {
		otherTestFrameTime = itOther->second.frameTime;
		otherTestCostPerCall = itOther->second.costPerCall;
	}
	auto itTotal = s_testData.find(-1);
	if (itTotal != s_testData.end()) {
		totalTestFrameTime = itTotal->second.frameTime;
		totalTestCostPerCall = itTotal->second.costPerCall;
	}
	DrawCallRow otherRow = {
		"Other:", -2, 0, otherFrameTime,
		(smoothedFrameTime > 0.0f ? static_cast<float>((otherFrameTime / smoothedFrameTime) * 100.0f) : 0.0f),
		0.0f,
		std::string("Frame time not attributed to any measured shader type. This includes UI, post-processing, engine work, and any GPU activity not directly measured by the overlay."),
		true, otherTestFrameTime, otherTestCostPerCall
	};
	DrawCallRow totalRow = {
		"Total:", -1, static_cast<int>(totalSmoothedDrawCalls), smoothedFrameTime, 100.0f,
		(totalSmoothedDrawCalls > 0.0f ? static_cast<float>(smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f),
		std::string("Sum of all measured shader types. Click to enable or disable all shaders."),
		true, totalTestFrameTime, totalTestCostPerCall
	};
	std::vector<DrawCallRow> summaryRows = { otherRow, totalRow };
	return { mainRows, summaryRows };
}

void PerformanceOverlay::DrawDrawCalls()
{
	static bool clearTestDataRequested = false;
	auto* menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();

	// helper for creating columns
	auto makeLiveMetricColumn = [&](auto valueGetter, auto colorGetter, auto formatter, const std::vector<std::string>& tooltipLines, const std::vector<ImVec4>& tooltipColors) {
		return [theme, valueGetter, colorGetter, formatter, tooltipLines, tooltipColors](const DrawCallRow& row, int) {
			float value = valueGetter(row);
			ImVec4 color = colorGetter(theme, value, row);
			std::string valueStr = formatter(value, row);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", valueStr.c_str());
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					Util::DrawMultiLineTooltip(tooltipLines, tooltipColors);
				}
			}
		};
	};

	auto makeTestMetricColumn = [&](auto valueGetter, auto colorGetter, auto formatter, const std::vector<std::string>& tooltipLines, const std::vector<ImVec4>& tooltipColors) {
		return [theme, valueGetter, colorGetter, formatter, tooltipLines, tooltipColors](const DrawCallRow& row, int) {
			auto opt = valueGetter(row);
			if (!opt.has_value()) {
				ImGui::TextDisabled("-");
				return;
			}
			float value = *opt;
			ImVec4 color = colorGetter(theme, value, row);
			std::string valueStr = formatter(value, row);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", valueStr.c_str());
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					Util::DrawMultiLineTooltip(tooltipLines, tooltipColors);
				}
			}
		};
	};

	this->CaptureTestData();

	bool anyTestData = !s_testData.empty();
	if (anyTestData) {
		if (ImGui::Button("Clear Test Data")) {
			clearTestDataRequested = true;
		}
	}

	// --- COLUMN CONFIG ---
	struct ColumnConfig
	{
		std::string header;
		std::function<void(const DrawCallRow&, int colIdx)> cellRender;
		std::function<bool(const DrawCallRow&, const DrawCallRow&, bool)> sortFunc;
		std::function<void()> headerTooltip;
	};

	// --- BUILD HEADERS AND CONFIG ---
	std::vector<ColumnConfig> columns = {
		{ "Shader Type",
			[theme](const DrawCallRow& row, int) {
				if (!row.enabled)
					ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.Disable);
				bool wasEnabled = row.enabled;
				if (ImGui::Selectable(row.label.c_str(), false)) {
					auto maybeType = magic_enum::enum_cast<RE::BSShader::Type>(row.shaderType);
					if (maybeType.has_value()) {
						auto classIndex = magic_enum::enum_integer(*maybeType) - 1;
						if (classIndex >= 0 && classIndex < magic_enum::enum_integer(RE::BSShader::Type::Total) - 1) {
							bool isDisabling = wasEnabled;
							float prevFrameTime = row.frameTime;
							float prevCostPerCall = row.costPerCall;
							// Capture live data for Total and Other before toggling
							float smoothedFrameTime = static_cast<float>(PerformanceOverlay::GetSingleton()->perfOverlayState.smoothFrameTimeMs);
							float totalSmoothedDrawCalls = static_cast<float>(globals::state->smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
							float totalCostPerCall = (totalSmoothedDrawCalls > 0.0f) ? (smoothedFrameTime / totalSmoothedDrawCalls) : 0.0f;
							float measuredSum = 0.0f;
							for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
								if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
									continue;
								int typeIndex = magic_enum::enum_integer(type);
								measuredSum += static_cast<float>(globals::state->smoothFrameTimePerType[typeIndex]);
							}
							float otherFrameTime = smoothedFrameTime - measuredSum;
							float otherPercent = (smoothedFrameTime > 0.0f) ? (otherFrameTime / smoothedFrameTime) * 100.0f : 0.0f;
							globals::state->enabledClasses[classIndex] = !wasEnabled;
							if (isDisabling) {
								// Save the last live value before disabling
								UpdateShaderTestData(row.shaderType, prevFrameTime, prevCostPerCall);
								// Save Total and Other test data as well
								PerformanceOverlay::s_testData[-1] = { smoothedFrameTime, totalCostPerCall, 100.0f };
								PerformanceOverlay::s_testData[-2] = { otherFrameTime, 0.0f, otherPercent };
								PerformanceOverlay::s_testDataSource = PerformanceOverlay::TestDataSource::ManualShaderToggle;
								PerformanceOverlay::s_testDataLastUpdated = std::chrono::steady_clock::now();
							}
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
			nullptr },
		{ "Draw Calls",
			[](const DrawCallRow& row, int) {
				ImGui::Text("%d", row.drawCalls);
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted("Draw Calls: Number of draw calls for this shader type in the current frame.");
					}
				}
			},
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.drawCalls < b.drawCalls) : (a.drawCalls > b.drawCalls); },
			[]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted("Draw Calls: Number of draw calls for this shader type in the current frame.");
					}
				}
			} }

	};
	columns.push_back(ColumnConfig{
		"Frame Time (%)",
		makeLiveMetricColumn(
			[](const DrawCallRow& row) { return row.frameTime; },
			[](const auto& theme, float value, const DrawCallRow&) { return Util::GetThresholdColor(value, 2.0f, 5.0f, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error); },
			[](float /*value*/, const DrawCallRow& row) {
				return Util::FormatMilliseconds(row.frameTime) + " (" + Util::FormatPercent(row.percent) + ")";
			},
			{ "Frame Time: Time spent on this shader type (ms and % of total frame time).", "", "Reference thresholds (ms):", "  < 2 ms", "  >= 2 ms and < 5 ms", "  >= 5 ms" },
			{ theme.Palette.Text, theme.Palette.Text, theme.Palette.Text, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error }),
		[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.percent < b.percent) : (a.percent > b.percent); },
		[theme]() {
			if (ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					std::vector<std::string> lines = { "Frame Time: Time spent on this shader type (ms and % of total frame time).", "", "Performance Color Legend (ms):", "  <= 2 ms", "  > 2 ms and <= 5 ms", "  > 5 ms" };
					std::vector<ImVec4> colors = { theme.Palette.Text, theme.Palette.Text, theme.Palette.Text, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error };
					Util::DrawMultiLineTooltip(lines, colors);
				}
			}
		} });

	columns.push_back(ColumnConfig{
		"Cost/Call",
		makeLiveMetricColumn(
			[](const DrawCallRow& row) { return row.costPerCall; },
			[](const auto& theme, float value, const DrawCallRow&) { return Util::GetThresholdColor(value, 0.05f, 0.2f, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error); },
			[](float value, const DrawCallRow&) { return (value < 0.01f && value > 0.0f) ? Util::FormatMicroseconds(value * 1000.0f) : Util::FormatMilliseconds(value); },
			{ "Cost/Call: Average time per draw call for this shader type.", "", "Reference thresholds (ms/call):", "  <= 0.05 ms/call", "  > 0.05 ms and <= 0.2 ms/call", "  > 0.2 ms/call" },
			{ theme.Palette.Text, theme.Palette.Text, theme.Palette.Text, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error }),
		[](const DrawCallRow& a, const DrawCallRow& b, bool asc) { return asc ? (a.costPerCall < b.costPerCall) : (a.costPerCall > b.costPerCall); },
		[theme]() {
			if (ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					std::vector<std::string> lines = { "Cost/Call: Average time per draw call for this shader type.", "", "Performance Color Legend (ms/call):", "  <= 0.05 ms/call", "  > 0.05 ms and <= 0.2 ms/call", "  > 0.2 ms/call" };
					std::vector<ImVec4> colors = { theme.Palette.Text, theme.Palette.Text, theme.Palette.Text, theme.StatusPalette.SuccessColor, theme.StatusPalette.Warning, theme.StatusPalette.Error };
					Util::DrawMultiLineTooltip(lines, colors);
				}
			}
		} });

	// Add test columns if present
	if (anyTestData) {
		columns.push_back(ColumnConfig{
			"Test Frame Time (%)",
			makeTestMetricColumn(
				[](const DrawCallRow& row) { return row.testFrameTime; },
				[](const auto& theme, float value, const DrawCallRow& row) {
					if (value < row.frameTime)
						return theme.StatusPalette.SuccessColor;
					if (value > row.frameTime)
						return theme.StatusPalette.Error;
					return theme.Palette.Text;
				},
				[](float value, const DrawCallRow& row) { return Util::FormatMilliseconds(value) + " (" + Util::FormatPercent(PerformanceOverlay::s_testData[row.shaderType].percent) + ")"; },
				{ PerformanceOverlay::GetTestDataTooltip(), "", "Color legend (compared to live data):" },
				{ theme.Palette.Text, theme.Palette.Text, theme.Palette.Text }),
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				float aVal = a.testFrameTime.value_or(FLT_MAX);
				float bVal = b.testFrameTime.value_or(FLT_MAX);
				return asc ? (aVal < bVal) : (aVal > bVal);
			},
			[theme]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(PerformanceOverlay::GetTestDataTooltip().c_str());
					}
				}
			} });

		columns.push_back(ColumnConfig{
			"Test Cost/Call",
			makeTestMetricColumn(
				[](const DrawCallRow& row) { return row.testCostPerCall; },
				[](const auto& theme, float value, const DrawCallRow& row) {
					if (value < row.costPerCall)
						return theme.StatusPalette.SuccessColor;
					if (value > row.costPerCall)
						return theme.StatusPalette.Error;
					return theme.Palette.Text;
				},
				[](float value, const DrawCallRow&) { return (value < 0.01f && value > 0.0f) ? Util::FormatMicroseconds(value * 1000.0f) : Util::FormatMilliseconds(value); },
				{ PerformanceOverlay::GetTestDataTooltip(), "", "Color legend (compared to live data):" },
				{ theme.Palette.Text, theme.Palette.Text, theme.Palette.Text }),
			[](const DrawCallRow& a, const DrawCallRow& b, bool asc) {
				float aVal = a.testCostPerCall.value_or(FLT_MAX);
				float bVal = b.testCostPerCall.value_or(FLT_MAX);
				return asc ? (aVal < bVal) : (aVal > bVal);
			},
			[theme]() {
				if (ImGui::IsItemHovered()) {
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(PerformanceOverlay::GetTestDataTooltip().c_str());
					}
				}
			} });
	}

	// --- BUILD ROWS ---
	auto [mainRows, summaryRows] = this->BuildDrawCallRows();

	// --- TABLE RENDER: MAIN ROWS + FOOTER ROWS ---
	std::vector<std::function<bool(const DrawCallRow&, const DrawCallRow&, bool)>> sorters;
	for (const auto& col : columns) sorters.push_back(col.sortFunc);
	Util::ShowSortedStringTable<DrawCallRow>(
		"DrawCallOverlayTable",
		[&columns]() { std::vector<std::string> h; for (const auto& c : columns) h.push_back(c.header); return h; }(),
		mainRows,
		0,
		true,
		sorters,
		[&columns](int rowIdx, int colIdx, const DrawCallRow& row) {
			(void)rowIdx;
			// Special handling for summary rows
			if ((row.label == "Total:" || row.label == "Other:") && colIdx == 0) {
				if (row.label == "Total:") {
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
						UpdateAllShaderTestData();
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
		},
		summaryRows);

	if (clearTestDataRequested) {  // clear after all drawing complete.
		ClearTestData();
		clearTestDataRequested = false;
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