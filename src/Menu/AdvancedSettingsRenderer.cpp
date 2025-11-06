#include "AdvancedSettingsRenderer.h"

#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <thread>

#include "FeatureIssues.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"
#include "Utils/UI.h"

void AdvancedSettingsRenderer::RenderAdvancedSettings(
	const std::function<void()>& drawTruePBRSettings,
	const std::function<void()>& drawDisableAtBootSettings,
	std::string& pbrTextureSetSearch,
	std::string& pbrMaterialObjectSearch)
{
	// Use TabBar system for better organization
	if (ImGui::BeginTabBar("##AdvancedSettingsTabs", ImGuiTabBarFlags_None)) {
		// Advanced Settings Tab
		if (MenuFonts::BeginTabItemWithFont("Advanced Settings", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##AdvancedSettingsContent", ImVec2(0, 0), false)) {
				RenderAdvancedSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Shader Replacement Tab
		if (MenuFonts::BeginTabItemWithFont("Shader Replacement", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##ShaderReplacementContent", ImVec2(0, 0), false)) {
				RenderShaderReplacementSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// PBR Settings Tab
		if (MenuFonts::BeginTabItemWithFont("PBR Settings", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##PBRSettingsContent", ImVec2(0, 0), false)) {
				RenderPBRSection(drawTruePBRSettings, pbrTextureSetSearch, pbrMaterialObjectSearch);
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Disable at Boot Tab
		if (MenuFonts::BeginTabItemWithFont("Disable at Boot", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##DisableAtBootContent", ImVec2(0, 0), false)) {
				RenderDisableAtBootSection(drawDisableAtBootSettings);
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Developer Tools Tab (only in developer mode)
		if (globals::state->IsDeveloperMode()) {
			if (MenuFonts::BeginTabItemWithFont("Developer Tools", Menu::FontRole::Subheading)) {
				if (ImGui::BeginChild("##DeveloperToolsContent", ImVec2(0, 0), false)) {
					RenderDeveloperSection();
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
		}

		ImGui::EndTabBar();
	}
}

void AdvancedSettingsRenderer::RenderAdvancedSection()
{
	auto shaderCache = globals::shaderCache;

	// Dump Shaders option
	bool useDump = shaderCache->IsDump();
	if (ImGui::Checkbox("Dump Shaders", &useDump)) {
		shaderCache->SetDump(useDump);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Dump shaders at startup. This should be used only when reversing shaders. Normal users don't need this.");
	}

	// Log Level selection
	spdlog::level::level_enum logLevel = globals::state->GetLogLevel();
	const char* items[] = {
		"trace",
		"debug",
		"info",
		"warn",
		"err",
		"critical",
		"off"
	};
	static int item_current = static_cast<int>(logLevel);
	if (ImGui::Combo("Log Level", &item_current, items, IM_ARRAYSIZE(items))) {
		ImGui::SameLine();
		globals::state->SetLogLevel(static_cast<spdlog::level::level_enum>(item_current));
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Log level. Trace is most verbose. Default is info.");
	}

	// Shader Defines input
	auto& shaderDefines = globals::state->shaderDefinesString;
	if (ImGui::InputText("Shader Defines", &shaderDefines)) {
		globals::state->SetDefines(shaderDefines);
	}
	if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemActive() &&
												   (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) ||
													   ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadEnter))))) {
		globals::state->SetDefines(shaderDefines);
		shaderCache->Clear();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Defines for Shader Compiler. Semicolon \";\" separated. Clear with space. Rebuild shaders after making change. Compute Shaders require a restart to recompile.");
	}

	ImGui::Spacing();

	// Compiler Thread controls
	ImGui::SliderInt("Compiler Threads", &shaderCache->compilationThreadCount, 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Number of threads to use to compile shaders. "
			"The more threads the faster compilation will finish but may make the system unresponsive. ");
	}
	ImGui::SliderInt("Background Compiler Threads", &shaderCache->backgroundCompilationThreadCount, 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Number of threads to use to compile shaders while playing game. "
			"This is activated if the startup compilation is skipped. "
			"The more threads the faster compilation will finish but may make the system unresponsive. ");
	}

	// A/B Testing settings
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->DrawSettingsUI();

	// File Watcher option
	bool useFileWatcher = shaderCache->UseFileWatcher();
	if (ImGui::Checkbox("Enable File Watcher", &useFileWatcher)) {
		shaderCache->SetFileWatcher(useFileWatcher);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Automatically recompile shaders on file change. "
			"Intended for developing.");
	}

	// Dump Ini Settings button
	if (ImGui::Button("Dump Ini Settings", { -1, 0 })) {
		Util::DumpSettingsOptions();
	}

	// Clear Shader Cache button
	if (ImGui::Button("Clear Shader Cache", { -1, 0 })) {
		shaderCache->Clear();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Clear all compiled shaders from memory. Forces recompilation of all shaders on next use.");
	}

	// Blocking shader controls
	if (!shaderCache->blockedKey.empty()) {
		auto blockingButtonString = std::format("Stop Blocking {} Shaders", shaderCache->blockedIDs.size());
		if (ImGui::Button(blockingButtonString.c_str(), { -1, 0 })) {
			shaderCache->DisableShaderBlocking();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Stop blocking Community Shaders shader. "
				"Blocking is helpful when debugging shader errors in game to determine which shader has issues. "
				"Blocking is enabled if in developer mode and pressing PAGEUP and PAGEDOWN. "
				"Specific shader will be printed to logfile. ");
		}
	}

	// Debug addresses section
	if (ImGui::TreeNodeEx("Addresses")) {
		auto Renderer = globals::game::renderer;
		auto BSShaderAccumulator = *globals::game::currentAccumulator.get();
		auto RendererShadowState = globals::game::shadowState;
		ADDRESS_NODE(Renderer)
		ADDRESS_NODE(BSShaderAccumulator)
		ADDRESS_NODE(RendererShadowState)
		ImGui::TreePop();
	}

	// Statistics section
	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Shader Compiler : {}", shaderCache->GetShaderStatsString()).c_str());
		ImGui::TreePop();
	}

	// Frame annotations toggle
	ImGui::Checkbox("Frame Annotations", &globals::state->frameAnnotations);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable detailed frame annotations for debugging render passes and draw calls.");
	}
}

void AdvancedSettingsRenderer::RenderShaderReplacementSection()
{
	auto state = globals::state;
	if (ImGui::BeginTable("##ReplaceToggles", 3, ImGuiTableFlags_SizingStretchSame)) {
		globals::state->ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
			ImGui::TableNextColumn();

			if (!(SIE::ShaderCache::IsSupportedShader(type) || state->IsDeveloperMode())) {
				ImGui::BeginDisabled();
				ImGui::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
				ImGui::EndDisabled();
			} else
				ImGui::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
		});
		if (state->IsDeveloperMode()) {
			ImGui::Checkbox("Vertex", &state->enableVShaders);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Replace Vertex Shaders. "
					"When false, will disable the custom Vertex Shaders for the types above. "
					"For developers to test whether CS shaders match vanilla behavior. ");
			}

			ImGui::Checkbox("Pixel", &state->enablePShaders);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Replace Pixel Shaders. "
					"When false, will disable the custom Pixel Shaders for the types above. "
					"For developers to test whether CS shaders match vanilla behavior. ");
			}

			ImGui::Checkbox("Compute", &state->enableCShaders);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Replace Compute Shaders. "
					"When false, will disable the custom Compute Shaders for the types above. "
					"For developers to test whether CS shaders match vanilla behavior. ");
			}
		}
		ImGui::EndTable();
	}
}

void AdvancedSettingsRenderer::RenderPBRSection(
	const std::function<void()>& drawTruePBRSettings,
	std::string& pbrTextureSetSearch,
	std::string& pbrMaterialObjectSearch)
{
	// Call TruePBR DrawSettings with search parameters
	drawTruePBRSettings();
}

void AdvancedSettingsRenderer::RenderDisableAtBootSection(const std::function<void()>& drawDisableAtBootSettings)
{
	drawDisableAtBootSettings();
}

void AdvancedSettingsRenderer::RenderDeveloperSection()
{
	// Developer Mode Testing Section
	if (globals::state->IsDeveloperMode()) {
		FeatureIssues::Test::DrawDeveloperModeTestingUI();
	}
}