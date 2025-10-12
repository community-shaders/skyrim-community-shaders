#include "AdvancedSettingsRenderer.h"

#include <algorithm>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <thread>

#include "FeatureIssues.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"
#include "Utils/UI.h"

void AdvancedSettingsRenderer::RenderAdvancedSettings(
	const std::function<void()>& drawTruePBRSettings,
	const std::function<void()>& drawDisableAtBootSettings)
{
	RenderAdvancedSection();
	RenderShaderReplacementSection();

	// TruePBR settings
	drawTruePBRSettings();

	// Disable at boot settings
	drawDisableAtBootSettings();

	RenderShaderDebugSection();
	RenderDeveloperSection();
}

void AdvancedSettingsRenderer::RenderAdvancedSection()
{
	auto shaderCache = globals::shaderCache;

	if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
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

		// Show shader blocking status (full controls in Shader Debugging section)
		if (globals::state->IsDeveloperMode() && !shaderCache->blockedKey.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
				"Shader Blocking Active: %zu shaders", shaderCache->blockedIDs.size());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("See 'Shader Debugging' section below for details and controls.");
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
	}
}

void AdvancedSettingsRenderer::RenderShaderReplacementSection()
{
	if (ImGui::CollapsingHeader("Replace Original Shaders", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
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
}

void AdvancedSettingsRenderer::RenderShaderDebugSection()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!state->IsDeveloperMode()) {
		return;
	}

	if (ImGui::CollapsingHeader("Shader Debugging", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		// Show currently blocked shader info
		if (!shaderCache->blockedKey.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Shader Blocking Active");
			ImGui::Separator();
			
			ImGui::Text("Blocked Shader:");
			ImGui::Indent();
			ImGui::TextWrapped("%s", shaderCache->blockedKey.c_str());
			ImGui::Text("Descriptors Blocked: %zu", shaderCache->blockedIDs.size());
			
			// Try to get more details from active shaders
			auto activeShaders = shaderCache->GetActiveShaders();
			for (const auto& shader : activeShaders) {
				if (shader.key == shaderCache->blockedKey) {
					ImGui::Text("Type: %s", magic_enum::enum_name(shader.shaderType).data());
					ImGui::Text("Class: %s", magic_enum::enum_name(shader.shaderClass).data());
					ImGui::Text("Descriptor: 0x%X", shader.descriptor);
					
					// Convert wstring to string for display
					std::string diskPathStr;
					diskPathStr.resize(shader.diskPath.size());
					std::transform(shader.diskPath.begin(), shader.diskPath.end(), diskPathStr.begin(),
						[](wchar_t c) { return static_cast<char>(c); });
					ImGui::Text("Cache Path: %s", diskPathStr.c_str());
					break;
				}
			}
			ImGui::Unindent();
			ImGui::Spacing();
			
			if (ImGui::Button("Stop Blocking", { -1, 0 })) {
				shaderCache->DisableShaderBlocking();
			}
			ImGui::Separator();
		}

		// Active shaders list
		ImGui::Text("Active Shaders (Used Recently)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"List of shaders that have been used in recent frames. "
				"Use PAGEUP/PAGEDOWN to cycle through and block shaders for debugging. "
				"Shaders not used for ~1 second are removed from this list.");
		}
		
		auto activeShaders = shaderCache->GetActiveShaders();
		ImGui::Text("Total Active: %zu", activeShaders.size());
		
		// Filter controls
		static char filterText[256] = "";
		ImGui::InputText("Filter", filterText, IM_ARRAYSIZE(filterText));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Filter shaders by key substring (case-sensitive)");
		}
		
		static int sortMode = 0;  // 0 = key, 1 = draw calls, 2 = type
		ImGui::Combo("Sort By", &sortMode, "Key\0Draw Calls\0Type\0");
		
		// Sort active shaders
		std::vector<SIE::ShaderCache::ActiveShaderInfo> sortedShaders = activeShaders;
		if (sortMode == 0) {
			std::sort(sortedShaders.begin(), sortedShaders.end(),
				[](const auto& a, const auto& b) { return a.key < b.key; });
		} else if (sortMode == 1) {
			std::sort(sortedShaders.begin(), sortedShaders.end(),
				[](const auto& a, const auto& b) { return a.drawCalls > b.drawCalls; });
		} else if (sortMode == 2) {
			std::sort(sortedShaders.begin(), sortedShaders.end(),
				[](const auto& a, const auto& b) {
					if (a.shaderType != b.shaderType)
						return a.shaderType < b.shaderType;
					return a.key < b.key;
				});
		}
		
		// Display shader list
		if (ImGui::BeginTable("##ActiveShaders", 5, 
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0, 300))) {
			
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Descriptor", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Draw Calls", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			
			std::string filterStr(filterText);
			for (const auto& shader : sortedShaders) {
				// Apply filter
				if (!filterStr.empty() && shader.key.find(filterStr) == std::string::npos) {
					continue;
				}
				
				ImGui::TableNextRow();
				
				// Type column
				ImGui::TableNextColumn();
				ImGui::Text("%s", magic_enum::enum_name(shader.shaderType).data());
				
				// Class column
				ImGui::TableNextColumn();
				auto classStr = magic_enum::enum_name(shader.shaderClass);
				if (classStr == "Vertex")
					ImGui::Text("V");
				else if (classStr == "Pixel")
					ImGui::Text("P");
				else if (classStr == "Compute")
					ImGui::Text("C");
				else
					ImGui::Text("%s", classStr.data());
				
				// Descriptor column
				ImGui::TableNextColumn();
				ImGui::Text("0x%X", shader.descriptor);
				
				// Draw calls column
				ImGui::TableNextColumn();
				ImGui::Text("%u", shader.drawCalls);
				
				// Key column with block button
				ImGui::TableNextColumn();
				bool isBlocked = (shader.key == shaderCache->blockedKey);
				if (isBlocked) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
				}
				
				ImGui::PushID(shader.key.c_str());
				if (ImGui::SmallButton(isBlocked ? "Unblock" : "Block")) {
					if (isBlocked) {
						shaderCache->DisableShaderBlocking();
					} else {
						shaderCache->blockedKey = shader.key;
						shaderCache->blockedKeyIndex = 0;  // Reset index
						shaderCache->blockedIDs.clear();
						logger::debug("Manually blocking shader: {}", shader.key);
					}
				}
				ImGui::PopID();
				
				ImGui::SameLine();
				ImGui::TextWrapped("%s", shader.key.c_str());
				
				if (isBlocked) {
					ImGui::PopStyleColor();
				}
				
				// Tooltip with full info
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text("Type: %s", magic_enum::enum_name(shader.shaderType).data());
					ImGui::Text("Class: %s", magic_enum::enum_name(shader.shaderClass).data());
					ImGui::Text("Descriptor: 0x%X", shader.descriptor);
					ImGui::Text("Draw Calls: %u", shader.drawCalls);
					ImGui::Text("Key: %s", shader.key.c_str());
					
					// Convert wstring to string for display
					std::string diskPathStr;
					diskPathStr.resize(shader.diskPath.size());
					std::transform(shader.diskPath.begin(), shader.diskPath.end(), diskPathStr.begin(),
						[](wchar_t c) { return static_cast<char>(c); });
					ImGui::Text("Cache Path: %s", diskPathStr.c_str());
					ImGui::EndTooltip();
				}
			}
			
			ImGui::EndTable();
		}
		
		ImGui::Spacing();
		ImGui::TextWrapped(
			"Tip: Use PAGEUP/PAGEDOWN keys to quickly cycle through active shaders. "
			"Blocked shaders will use vanilla rendering instead of Community Shaders.");
	}
}

void AdvancedSettingsRenderer::RenderDeveloperSection()
{
	// Developer Mode Testing Section
	if (globals::state->IsDeveloperMode()) {
		FeatureIssues::Test::DrawDeveloperModeTestingUI();
	}
}