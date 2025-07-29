#include "ABTesting.h"
#include "Features/PerformanceOverlay.h"
#include "Menu.h"
#include "State.h"
#include "Utils/UI.h"
#include <fmt/format.h>
#include <imgui.h>

ABTestingManager* ABTestingManager::GetSingleton()
{
	static ABTestingManager singleton;
	return &singleton;
}

void ABTestingManager::SetTestInterval(uint32_t interval)
{
	testInterval = interval;
}

void ABTestingManager::Enable()
{
	if (!abTestingEnabled) {
		auto* state = globals::state;
		auto& performanceOverlay = globals::features::performanceOverlay;

		logger::info("Saving current settings for Variant B (TEST) and starting test with interval {}.", testInterval);
		state->Save(State::ConfigMode::TEST);
		abTestingEnabled = true;

		// Initialize QueryPerformanceCounter timing
		if (timingFrequency.QuadPart == 0) {
			QueryPerformanceFrequency(&timingFrequency);
		}
		QueryPerformanceCounter(&lastTestSwitch);

		// Preserve overlay enabled state
		bool overlayWasEnabled = performanceOverlay.settings.ShowInOverlay;
		performanceOverlay.settings.ShowInOverlay = overlayWasEnabled;
	}
}

void ABTestingManager::Disable()
{
	if (abTestingEnabled) {
		auto* state = globals::state;
		auto& performanceOverlay = globals::features::performanceOverlay;

		logger::info("Disabling A/B testing. Will restore to Variant B (TEST) config.");
		state->Load(State::ConfigMode::TEST);  // restore last settings before entering test mode
		abTestingEnabled = false;

		// Preserve overlay enabled state
		bool overlayWasEnabled = performanceOverlay.settings.ShowInOverlay;
		performanceOverlay.settings.ShowInOverlay = overlayWasEnabled;
	}
}

void ABTestingManager::Update()
{
	if (!abTestingEnabled)
		return;

	auto* state = globals::state;
	auto& performanceOverlay = globals::features::performanceOverlay;

	// Preserve overlay enabled state when switching configs
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);
	float seconds = (currentTime.QuadPart - lastTestSwitch.QuadPart) / static_cast<float>(timingFrequency.QuadPart);
	auto remaining = static_cast<float>(testInterval) - seconds;

	if (remaining < 0.0f) {
		bool overlayWasEnabled = performanceOverlay.settings.ShowInOverlay;
		usingTestConfig = !usingTestConfig;
		logger::info("Swapping to {} (A/B Test): {}",
			usingTestConfig ? "Variant B (TEST)" : "Variant A (USER)",
			usingTestConfig ? "TEST config" : "USER config");
		state->Load(usingTestConfig ? State::ConfigMode::TEST : State::ConfigMode::USER);
		performanceOverlay.settings.ShowInOverlay = overlayWasEnabled;  // Restore overlay state
		QueryPerformanceCounter(&lastTestSwitch);

		// Notify the A/B test aggregator of the variant switch
		aggregator.OnABSwitch(usingTestConfig ? ABVariant::B : ABVariant::A);
	}
}

void ABTestingManager::DrawSettingsUI()
{
	auto& performanceOverlay = globals::features::performanceOverlay;

	if (ImGui::SliderInt("A/B Test Interval", reinterpret_cast<int*>(&testInterval), 0, 10)) {
		bool overlayWasEnabled = performanceOverlay.settings.ShowInOverlay;
		if (testInterval == 0) {
			Disable();
		} else if (!abTestingEnabled) {
			Enable();
		} else {
			logger::info("Setting new A/B test interval {}.", testInterval);
		}
		performanceOverlay.settings.ShowInOverlay = overlayWasEnabled;
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Sets number of seconds before toggling between Variant A (USER) and Variant B (TEST) config for A/B testing. "
			"0 disables. Non-zero will enable A/B testing mode. "
			"Enabling will save current settings as TEST config (Variant B). "
			"This has no impact if no settings are changed. "
			"Variant A = USER config, Variant B = TEST config.");
	}
}

void ABTestingManager::DrawOverlayUI()
{
	if (!abTestingEnabled)
		return;

	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);
	float seconds = (currentTime.QuadPart - lastTestSwitch.QuadPart) / static_cast<float>(timingFrequency.QuadPart);
	auto remaining = static_cast<float>(testInterval) - seconds;

	ImGui::SetNextWindowBgAlpha(1.0f);
	ImGui::SetNextWindowPos(ImVec2(10, 10));
	if (!ImGui::Begin("Testing", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	remaining = std::max(0.0f, remaining);
	ImGui::Text(fmt::format("{} : {:.1f} seconds left",
		usingTestConfig ? "Variant B (TEST)" : "Variant A (USER)", remaining)
			.c_str());
	ImGui::End();
}