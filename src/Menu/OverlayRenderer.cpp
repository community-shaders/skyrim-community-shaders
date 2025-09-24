#include "OverlayRenderer.h"
#include "HomePageRenderer.h"
#include "ThemeManager.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Feature.h"
#include "FeatureIssues.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"

#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/VR.h"

void OverlayRenderer::RenderOverlay(
	Menu& menu,
	const std::function<void()>& processInputEventQueue,
	const std::function<void()>& drawSettings,
	const std::function<const char*(uint32_t)>& keyIdToString,
	float& cachedFontSize,
	float currentFontSize)
{
	HandleVRSetup();
	processInputEventQueue();

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.ProcessControllerInputForImGui();
	}

	if (ShouldSkipRendering()) {
		auto& io = ImGui::GetIO();
		io.ClearInputKeys();
		io.ClearEventsQueue();
		return;
	}

	HandleFontReload(menu, cachedFontSize, currentFontSize);
	InitializeImGuiFrame(menu);

	RenderShaderCompilationStatus(keyIdToString);
	RenderFirstTimeSetupOverlay();

	if (menu.IsEnabled || HomePageRenderer::ShouldShowFirstTimeSetup()) {
		ImGui::GetIO().MouseDrawCursor = true;
		if (menu.IsEnabled) {
			drawSettings();
		}
	} else {
		ImGui::GetIO().MouseDrawCursor = false;
	}

	RenderFeatureOverlays();
	HandleABTesting();
	FinalizeImGuiFrame();
}

void OverlayRenderer::HandleVRSetup()
{
	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.RecreateOverlayTexturesIfNeeded();
	}
}

bool OverlayRenderer::ShouldSkipRendering()
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetFailedTasks();
	auto hide = shaderCache->IsHideErrors();
	auto* abTestingManager = ABTestingManager::GetSingleton();

	return !(shaderCache->IsCompiling() ||
			 Menu::GetSingleton()->IsEnabled ||
			 abTestingManager->IsEnabled() ||
			 (failed && !hide) ||
			 globals::features::performanceOverlay.settings.ShowInOverlay);
}

void OverlayRenderer::HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize)
{
	// Reload font if user changed something
	if (std::abs(cachedFontSize - currentFontSize) > ThemeManager::Constants::FONT_CACHE_EPSILON) {
		ThemeManager::ReloadFont(menu, cachedFontSize);
	}
}

void OverlayRenderer::InitializeImGuiFrame(Menu& menu)
{
	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ThemeManager::SetupImGuiStyle(menu);
}

void OverlayRenderer::RenderShaderCompilationStatus(const std::function<const char*(uint32_t)>& keyIdToString)
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetFailedTasks();
	auto hide = shaderCache->IsHideErrors();

	uint64_t totalShaders = shaderCache->GetTotalTasks();
	uint64_t compiledShaders = shaderCache->GetCompletedTasks();

	auto state = globals::state;
	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	auto progressTitle = fmt::format("{}Compiling Shaders: {}",
		shaderCache->backgroundCompilation ? "Background " : "",
		shaderCache->GetShaderStatsString(!state->IsDeveloperMode()).c_str());
	auto percent = (float)compiledShaders / (float)totalShaders;
	auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", compiledShaders, totalShaders, 100 * percent);

	if (shaderCache->IsCompiling()) {
		ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION));
		if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());
		if (!shaderCache->backgroundCompilation && shaderCache->menuLoaded) {
			auto skipShadersText = fmt::format(
				"Press {} to proceed without completing shader compilation. ",
				keyIdToString(Menu::GetSingleton()->GetSettings().SkipCompilationKey));
			ImGui::TextUnformatted(skipShadersText.c_str());
			ImGui::TextUnformatted("WARNING: Uncompiled shaders will have visual errors or cause stuttering when loading.");
		}

		ImGui::End();
	} else if (failed) {
		if (!hide) {
			ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION));
			if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::End();
				return;
			}

			ImGui::TextColored(themeSettings.StatusPalette.Error, "ERROR: %d shaders failed to compile. Check installation and CommunityShaders.log", failed, totalShaders);

			// Check for features that may cause shader compilation issues
			if (FeatureIssues::HasPotentialShaderModifyingFeatures()) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "Features that may have modified shaders detected. Check Feature Issues in the Menu.");
			}

			ImGui::End();
		}
	}
}

void OverlayRenderer::RenderFeatureOverlays()
{
	// load overlays
	for (Feature* feat : Feature::GetFeatureList()) {
		if (feat && feat->loaded) {
			if (auto* overlay = dynamic_cast<OverlayFeature*>(feat)) {
				overlay->DrawOverlay();
			}
		}
	}
}

void OverlayRenderer::HandleABTesting()
{
	// A/B Testing management
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->Update();

	// Always update test data during TEST phase, regardless of overlay visibility
	if (abTestingManager->IsEnabled()) {
		globals::features::performanceOverlay.UpdateAllShaderTestData();

		// Add A/B test aggregator data collection here
		auto& overlay = globals::features::performanceOverlay;
		auto [mainRows, summaryRows] = overlay.BuildDrawCallRows();
		std::vector<DrawCallRow> allRows = mainRows;
		allRows.insert(allRows.end(), summaryRows.begin(), summaryRows.end());

		// Update the A/B test aggregator with current frame data
		abTestingManager->GetAggregator().OnFrame(allRows);
	}

	// Draw A/B testing overlay
	abTestingManager->DrawOverlayUI();
}

void OverlayRenderer::FinalizeImGuiFrame()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.SubmitOverlayFrame();
	}
}

void OverlayRenderer::RenderFirstTimeSetupOverlay()
{
	if (HomePageRenderer::ShouldShowFirstTimeSetup()) {
		HomePageRenderer::RenderFirstTimeSetupDialog();
	}
}