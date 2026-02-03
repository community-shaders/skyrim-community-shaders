#include "OverlayRenderer.h"
#include "BackgroundBlur.h"
#include "HomePageRenderer.h"
#include "ThemeManager.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <winrt/base.h>

#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/RenderDoc.h"
#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

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
	RenderShaderBlockingStatus();
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
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();
	auto* abTestingManager = ABTestingManager::GetSingleton();
	auto* renderDoc = RenderDoc::GetSingleton();

	return !(shaderCache->IsCompiling() ||
			 Menu::GetSingleton()->IsEnabled ||
			 abTestingManager->IsEnabled() ||
			 (failed && !hide) ||
			 globals::features::performanceOverlay.settings.ShowInOverlay ||
			 renderDoc->IsAvailable());
}

void OverlayRenderer::HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize)
{
	bool fontSizeChanged = std::abs(cachedFontSize - currentFontSize) > ThemeManager::Constants::FONT_CACHE_EPSILON;
	std::string desiredSignature = menu.BuildFontSignature(currentFontSize);
	bool signatureChanged = desiredSignature != menu.cachedFontSignature;

	if (fontSizeChanged || signatureChanged) {
		if (!ThemeManager::ReloadFont(menu, cachedFontSize)) {
			logger::warn("OverlayRenderer::HandleFontReload() - Font reload failed");
		}
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
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();

	uint64_t totalShaders = shaderCache->GetTotalTasks();
	uint64_t compiledShaders = shaderCache->GetCompletedTasks();

	auto state = globals::state;
	auto& themeSettings = Menu::GetSingleton()->GetTheme();
	auto* renderDoc = RenderDoc::GetSingleton();
	bool renderDocAvailable = renderDoc->IsAvailable();
	const auto renderDocInformation = renderDoc->GetOverlayWarningMessage();

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

		if (renderDocAvailable)
			ImGui::TextColored(themeSettings.StatusPalette.Warning, renderDocInformation.c_str());

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

			if (renderDocAvailable)
				ImGui::TextColored(themeSettings.StatusPalette.Warning, renderDocInformation.c_str());

			ImGui::End();
		}
	} else if (renderDocAvailable) {
		ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION));
		if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextColored(themeSettings.StatusPalette.Warning, renderDocInformation.c_str());
		ImGui::End();
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

	// Apply background blur behind ImGui windows before rendering them
	BackgroundBlur::RenderBackgroundBlur();

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

void OverlayRenderer::RenderShaderBlockingStatus()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!state->IsDeveloperMode() || shaderCache->blockedKey.empty()) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION + 100));
	if (!ImGui::Begin("ShaderBlockingInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	ImGui::TextColored(Util::Colors::GetError(), "Shader Blocking Active");
	ImGui::Text("Blocked: %s", shaderCache->blockedKey.c_str());

	// Try to get more details from active shaders
	auto activeShaders = shaderCache->GetActiveShaders();

	// Find the index of the blocked shader in the active list (or show N/A if not found)
	size_t blockedIndex = 0;
	bool foundBlocked = false;
	for (size_t i = 0; i < activeShaders.size(); ++i) {
		if (activeShaders[i].key == shaderCache->blockedKey) {
			blockedIndex = i + 1;  // 1-based indexing for display
			foundBlocked = true;
			break;
		}
	}

	if (foundBlocked) {
		ImGui::Text("Index: %zu/%zu", blockedIndex, activeShaders.size());
	} else {
		ImGui::Text("Index: N/A (%zu active)", activeShaders.size());
	}

	for (const auto& shader : activeShaders) {
		if (shader.key == shaderCache->blockedKey) {
			ImGui::Text("Type: %s | Class: %s | Descriptor: 0x%X",
				magic_enum::enum_name(shader.shaderType).data(),
				magic_enum::enum_name(shader.shaderClass).data(),
				shader.descriptor);
			break;
		}
	}

	ImGui::End();
}