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

	if (!globals::state->IsDeveloperMode()) {
		return;
	}

	// Show blocked shader status as a regular section
	if (!shaderCache->blockedKey.empty()) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.1f, 0.1f, 0.8f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

		if (ImGui::CollapsingHeader("Currently Blocked Shader", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::TextColored(Util::Colors::GetError(), "Shader Blocking Active");
			ImGui::SameLine();
			if (ImGui::SmallButton("Stop Blocking##Section")) {
				shaderCache->DisableShaderBlocking();
			}

			ImGui::Text("Blocked: %s", shaderCache->blockedKey.c_str());

			// Try to get more details from active shaders
			auto activeShaders = shaderCache->GetActiveShaders();
			for (const auto& shader : activeShaders) {
				if (shader.key == shaderCache->blockedKey) {
					ImGui::Text("Type: %s | Class: %s | Descriptor: 0x%X",
						magic_enum::enum_name(shader.shaderType).data(),
						magic_enum::enum_name(shader.shaderClass).data(),
						shader.descriptor);

					// Add copy button with full information including disk cache
					ImGui::SameLine();
					ImGui::PushID("copy_blocked_shader");
					if (ImGui::SmallButton("Copy Info")) {
						// Convert wstring to string for display
						std::string diskPathStr;
						diskPathStr.resize(shader.diskPath.size());
						std::transform(shader.diskPath.begin(), shader.diskPath.end(), diskPathStr.begin(),
							[](wchar_t c) { return static_cast<char>(c); });

						std::string fullInfo = std::format("Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\nCache Path: {}",
							magic_enum::enum_name(shader.shaderType).data(),
							magic_enum::enum_name(shader.shaderClass).data(),
							shader.descriptor,
							shader.key,
							diskPathStr);
						ImGui::SetClipboardText(fullInfo.c_str());
					}
					ImGui::PopID();
					if (ImGui::IsItemHovered()) {
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("Copy complete shader information including cache path to clipboard");
						}
					}

					break;
				}
			}
		}

		ImGui::PopStyleVar();    // WindowBorderSize
		ImGui::PopStyleColor();  // WindowBg
	}

	// Active shaders list
	if (ImGui::CollapsingHeader("Active Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Active Shaders (Used Recently)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"List of shaders that have been used in recent frames. "
				"Use PAGEUP/PAGEDOWN to cycle through and block shaders for debugging. "
				"Shaders not used for ~1 second are removed from this list.");
		}

		// Get fresh active shaders data for accurate count and table
		auto activeShaders = shaderCache->GetActiveShaders();
		ImGui::Text("Total Active: %zu", activeShaders.size());

		// Calculate total draw calls for percentage calculation
		uint32_t totalDrawCalls = 0;
		for (const auto& shader : activeShaders) {
			totalDrawCalls += shader.drawCalls;
		}

		// Filter controls (now handled by ShowFilteredStringTableCustom)
		static char filterText[256] = "";
		static int searchColumn = 0;  // 0 = All Columns, 1 = Type, 2 = Class, 3 = Descriptor, 4 = Draw Calls, 5 = Key

		// Create shader rows for the table utility (simplified - no filter data needed)
		struct ShaderRow
		{
			SIE::ShaderCache::ActiveShaderInfo shader;
			uint32_t totalDrawCalls;
		};

		std::vector<ShaderRow> shaderRows;
		for (const auto& shader : activeShaders) {
			shaderRows.push_back({ shader, totalDrawCalls });
		}

		// Build column configurations
		std::vector<Util::TableColumnConfig<ShaderRow>> columns = {
			{ "Type", "Shader type", [](const ShaderRow& row) {
				 return std::string(magic_enum::enum_name(row.shader.shaderType));
			 } },
			{ "Class", "Shader class", [](const ShaderRow& row) {
				 return std::string(magic_enum::enum_name(row.shader.shaderClass));
			 } },
			{ "Descriptor", "Shader descriptor hash", [](const ShaderRow& row) {
				 return std::format("0x{:X}", row.shader.descriptor);
			 } },
			{ "Frame %", "Percentage of total draw calls in current frame", [](const ShaderRow& row) {
				 float percentage = Util::CalculatePercentage(static_cast<float>(row.shader.drawCalls), static_cast<float>(row.totalDrawCalls));
				 return Util::FormatPercent(percentage);
			 } },
			{ "Key", "Shader key", [](const ShaderRow& row) {
				 return row.shader.key;
			 } }
		};

		// Row click callbacks
		auto onRowLeftClick = [shaderCache](const ShaderRow& row) {
			if (row.shader.key == shaderCache->blockedKey) {
				// Clicking on already blocked shader - unblock it
				shaderCache->DisableShaderBlocking();
			} else {
				// Clicking on different shader - block it
				shaderCache->blockedKey = row.shader.key;
				shaderCache->blockedKeyIndex = 0;
				shaderCache->blockedIDs.clear();
				logger::debug("Manually blocking shader: {}", row.shader.key);
			}
		};

		auto onRowRightClick = [shaderCache](const ShaderRow& row) {
			// Convert wstring to string for display
			std::string diskPathStr;
			diskPathStr.resize(row.shader.diskPath.size());
			std::transform(row.shader.diskPath.begin(), row.shader.diskPath.end(), diskPathStr.begin(),
				[](wchar_t c) { return static_cast<char>(c); });

			std::string fullInfo = std::format("Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\nCache Path: {}",
				magic_enum::enum_name(row.shader.shaderType).data(),
				magic_enum::enum_name(row.shader.shaderClass).data(),
				row.shader.descriptor,
				row.shader.key,
				diskPathStr);
			ImGui::SetClipboardText(fullInfo.c_str());
		};

		auto getRowTooltip = [shaderCache](const ShaderRow& row) {
			std::string clickAction = (row.shader.key == shaderCache->blockedKey) ? "Left-click to unblock this shader" : "Left-click to block this shader";

			return std::format("Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\n\n{}",
				magic_enum::enum_name(row.shader.shaderType).data(),
				magic_enum::enum_name(row.shader.shaderClass).data(),
				row.shader.descriptor,
				row.shader.key,
				clickAction);
		};

		// Define function to extract filterable fields (for TableFilterState)
		auto getFilterableFields = [](const ShaderRow& row) -> std::vector<std::string> {
			return {
				std::string(magic_enum::enum_name(row.shader.shaderType)),                                                                         // Type
				std::string(magic_enum::enum_name(row.shader.shaderClass)),                                                                        // Class
				std::format("0x{:X}", row.shader.descriptor),                                                                                      // Descriptor
				Util::FormatPercent(Util::CalculatePercentage(static_cast<float>(row.shader.drawCalls), static_cast<float>(row.totalDrawCalls))),  // Frame %
				row.shader.key                                                                                                                     // Key
			};
		};

		// Define sorting comparators (customSorts parameter)
		std::vector<std::function<bool(const ShaderRow&, const ShaderRow&, bool)>> sorters = {
			// Type - string sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				std::string aVal = std::string(magic_enum::enum_name(a.shader.shaderType));
				std::string bVal = std::string(magic_enum::enum_name(b.shader.shaderType));
				return ascending ? (aVal < bVal) : (aVal > bVal);
			},
			// Class - string sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				std::string aVal = std::string(magic_enum::enum_name(a.shader.shaderClass));
				std::string bVal = std::string(magic_enum::enum_name(b.shader.shaderClass));
				return ascending ? (aVal < bVal) : (aVal > bVal);
			},
			// Descriptor - numeric sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				return ascending ? (a.shader.descriptor < b.shader.descriptor) : (a.shader.descriptor > b.shader.descriptor);
			},
			// Frame % - numeric sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				float aPercent = Util::CalculatePercentage(static_cast<float>(a.shader.drawCalls), static_cast<float>(a.totalDrawCalls));
				float bPercent = Util::CalculatePercentage(static_cast<float>(b.shader.drawCalls), static_cast<float>(b.totalDrawCalls));
				return ascending ? (aPercent < bPercent) : (aPercent > bPercent);
			},
			// Key - string sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				return ascending ? (a.shader.key < b.shader.key) : (a.shader.key > b.shader.key);
			}
		};

		// Create filter state
		Util::TableFilterState<ShaderRow> filterState(getFilterableFields);

		// Initialize filter state from existing variables
		filterState.filterText = std::string(filterText, filterText + strlen(filterText));
		filterState.searchColumn = searchColumn;

		// Define input events for row interactions
		std::vector<Util::TableInputEvent<ShaderRow>> inputEvents = {
			// Left-click to block/unblock shader
			{ Util::TableInputEventType::MouseClick, onRowLeftClick, "", 0 },
			// Right-click context menu for copying info
			{ Util::TableInputEventType::ContextMenu, onRowRightClick, "Copy Info", 1 }
		};

		// Define function to get row text color (highlight blocked shaders)
		auto getRowTextColor = [shaderCache](const ShaderRow& row) -> ImVec4 {
			if (row.shader.key == shaderCache->blockedKey) {
				// Use theme error color for blocked shader text
				return Util::Colors::GetError();
			}
			return ImVec4(0, 0, 0, 0);  // Default text color for normal rows
		};

		// Use the new interactive table
		Util::ShowInteractiveTable<ShaderRow>(
			"ActiveShadersTable",
			columns,
			shaderRows,
			4,     // Default sort column (Key)
			true,  // Default ascending
			sorters,
			filterState,
			inputEvents,
			getRowTooltip,
			nullptr,           // No background color
			getRowTextColor);  // Pass the new text color function

		// Update the filter text back to the char array
		strncpy_s(filterText, filterState.filterText.c_str(), sizeof(filterText) - 1);
		searchColumn = filterState.searchColumn;
	}
}

void AdvancedSettingsRenderer::RenderDeveloperSection()
{
	// Developer Mode Testing Section
	if (globals::state->IsDeveloperMode()) {
		FeatureIssues::Test::DrawDeveloperModeTestingUI();
	}
}