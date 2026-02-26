#include "AdvancedSettingsRenderer.h"

#include <algorithm>
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
#include "Utils/Format.h"
#include "Utils/UI.h"

void AdvancedSettingsRenderer::RenderAdvancedSettings(
	const std::function<void()>& drawTruePBRSettings,
	const std::function<void()>& drawDisableAtBootSettings)
{
	// Use TabBar system - tabs sorted alphabetically
	if (ImGui::BeginTabBar("##AdvancedSettingsTabs", ImGuiTabBarFlags_None)) {
		// Developer Tab
		if (MenuFonts::BeginTabItemWithFont("Developer", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##DeveloperContent", ImVec2(0, 0), false)) {
				RenderDeveloperSection();
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

		// Logging Tab
		if (MenuFonts::BeginTabItemWithFont("Logging", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##LoggingContent", ImVec2(0, 0), false)) {
				RenderLoggingSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// PBR Settings Tab
		if (MenuFonts::BeginTabItemWithFont("PBR Settings", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##PBRSettingsContent", ImVec2(0, 0), false)) {
				RenderPBRSection(drawTruePBRSettings);
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		// Shader Debug Tab
		if (MenuFonts::BeginTabItemWithFont("Shader Debug", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##ShaderDebugContent", ImVec2(0, 0), false)) {
				RenderShaderDebugSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Testing Tab (for A/B Testing and related settings)
		if (MenuFonts::BeginTabItemWithFont("Testing", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##Testing", ImVec2(0, 0), false)) {
				RenderTestingSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void AdvancedSettingsRenderer::RenderLoggingSection()
{
	auto shaderCache = globals::shaderCache;

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

	ImGui::Columns(2, nullptr, false);

	// Dump Ini Settings button
	if (ImGui::Button("Dump Ini Settings", { -1, 0 })) {
		Util::DumpSettingsOptions();
	}

	ImGui::NextColumn();

	// Open Logs button
	std::filesystem::path logPath = Util::PathHelpers::GetLogPath();
	if (!logPath.empty() && ImGui::Button("Open Logs", { -1, 0 })) {
		ShellExecuteA(NULL, "open", logPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::Columns(1);
}

void AdvancedSettingsRenderer::RenderShaderDebugSection()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	// Dump Shaders option
	bool useDump = shaderCache->IsDump();
	if (ImGui::Checkbox("Dump Shaders", &useDump)) {
		shaderCache->SetDump(useDump);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Dump shaders at startup. This should be used only when reversing shaders. Normal users don't need this.");
	}

	// Clear Shader Cache button
	if (ImGui::Button("Clear Shader Cache", { -1, 0 })) {
		shaderCache->Clear();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Clear all compiled shaders from memory. Forces recompilation of all shaders on next use.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Shader Replacement section
	Util::DrawSectionHeader("Replace Original Shaders");

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

	// Only show shader blocking section in developer mode
	if (!globals::state->IsDeveloperMode()) {
		return;
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Show blocked shader status as a regular section
	if (!shaderCache->blockedKey.empty()) {
		// Create a visually distinct box for the blocked shader info with rounded corners and border
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		ImVec4 blockedBgColor = Util::Colors::GetError();
		blockedBgColor.w = 0.15f;  // Semi-transparent background
		ImGui::PushStyleColor(ImGuiCol_ChildBg, blockedBgColor);

		float maxHeight = ImGui::GetContentRegionAvail().y * 0.3f;  // Limit to 30% to keep Active Shaders visible
		if (ImGui::BeginChild("##BlockedShaderInfo", ImVec2(0, maxHeight), true, ImGuiChildFlags_AutoResizeY)) {
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
					ImGui::Text("Type: %s", magic_enum::enum_name(shader.shaderType).data());
					ImGui::Text("Class: %s", magic_enum::enum_name(shader.shaderClass).data());
					ImGui::Text("Descriptor: 0x%X", shader.descriptor);

					// Add button to copy shader info to clipboard
					ImGui::PushID(shader.key.c_str());
					if (ImGui::SmallButton("Copy Info##BlockedShader")) {
						std::string diskPathStr;
						diskPathStr.reserve(shader.diskPath.size());
						for (wchar_t wc : shader.diskPath) {
							diskPathStr += static_cast<char>(wc);
						}

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
		ImGui::EndChild();

		ImGui::PopStyleVar();    // ChildRounding
		ImGui::PopStyleVar();    // WindowBorderSize
		ImGui::PopStyleColor();  // ChildBg
	}

	// Shader Debug section
	if (ImGui::CollapsingHeader("Shader Debug")) {
		auto menu = globals::menu;
		auto& menuSettings = menu->GetSettings();
		auto& themeSettings = menuSettings.Theme;

		if (ImGui::Checkbox("Enable Shader Blocking", &menuSettings.EnableShaderBlocking)) {
			// Setting saved automatically on next save
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables hotkeys to cycle through and block individual shaders for debugging purposes.");
		}

		if (menuSettings.EnableShaderBlocking) {
			ImGui::Indent();

			// Shader Block Previous Key
			if (menu->settingShaderBlockPrevKey) {
				ImGui::Text("Press any key for Shader Block Previous...");
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Block Previous:");
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s",
					Util::Input::KeyIdToString(menuSettings.ShaderBlockPrevKey).c_str());
				ImGui::SameLine();
				if (ImGui::Button("Change##ShaderBlockPrev")) {
					menu->settingShaderBlockPrevKey = true;
				}
			}

			// Shader Block Next Key
			if (menu->settingShaderBlockNextKey) {
				ImGui::Text("Press any key for Shader Block Next...");
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Block Next:");
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s",
					Util::Input::KeyIdToString(menuSettings.ShaderBlockNextKey).c_str());
				ImGui::SameLine();
				if (ImGui::Button("Change##ShaderBlockNext")) {
					menu->settingShaderBlockNextKey = true;
				}
			}

			ImGui::Unindent();
		}
	}

	// Active shaders list
	if (ImGui::CollapsingHeader("Active Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Active Shaders (Used Recently)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"List of shaders that have been used in recent frames. "
				"Enable Shader Blocking above to use hotkeys to cycle through and block shaders for debugging. "
				"Shaders not used for ~1 second are removed from this list.");
		}

		// Get fresh active shaders data for accurate count and table
		auto activeShaders = shaderCache->GetActiveShaders();
		uint32_t totalDrawCalls = 0;
		for (const auto& shader : activeShaders) {
			totalDrawCalls += shader.drawCalls;
		}

		// Static variables to maintain table filter state
		static char filterText[256] = "";
		static int searchColumn = 0;        // 0 = All Columns, 1 = Type, 2 = Class, 3 = Descriptor, 4 = Draw Calls, 5 = Key
		static size_t sortColumn = 4;       // Default sort by Frame % (draw calls)
		static bool sortAscending = false;  // Descending by default (highest usage first)		// Create shader rows for the table utility (simplified - no filter data needed)
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
			{ "Descriptor", "Shader descriptor", [](const ShaderRow& row) {
				 return std::format("0x{:X}", row.shader.descriptor);
			 } },
			{ "Frame %", "Percentage of draw calls this frame", [](const ShaderRow& row) {
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
				shaderCache->DisableShaderBlocking();
			} else {
				// Block this shader - use IterateShaderBlock to find and block it
				// Or set blockedKey directly (simpler for click-to-block)
				shaderCache->blockedKey = row.shader.key;
				logger::info("Blocking shader: {}", row.shader.key);
			}
		};

		auto onRowRightClick = [shaderCache](const ShaderRow& row) {
			std::string diskPathStr;
			diskPathStr.reserve(row.shader.diskPath.size());
			for (wchar_t wc : row.shader.diskPath) {
				diskPathStr += static_cast<char>(wc);
			}

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

		// Render the table with all configurations
		Util::ShowInteractiveTable<ShaderRow>(
			"##ActiveShadersTable",
			columns,
			shaderRows,
			sortColumn,
			sortAscending,
			sorters,
			filterState,
			inputEvents,
			getRowTooltip);

		// Update static variables with modified filter state
		strncpy_s(filterText, filterState.filterText.c_str(), sizeof(filterText) - 1);
		filterText[sizeof(filterText) - 1] = '\0';
		searchColumn = filterState.searchColumn;
	}
}

void AdvancedSettingsRenderer::RenderPBRSection(const std::function<void()>& drawTruePBRSettings)
{
	drawTruePBRSettings();
}

void AdvancedSettingsRenderer::RenderDisableAtBootSection(const std::function<void()>& drawDisableAtBootSettings)
{
	drawDisableAtBootSettings();
}

void AdvancedSettingsRenderer::RenderDeveloperSection()
{
	auto shaderCache = globals::shaderCache;

	// File Watcher option (moved from Advanced/Logging)
	bool useFileWatcher = shaderCache->UseFileWatcher();
	if (ImGui::Checkbox("Enable File Watcher", &useFileWatcher)) {
		shaderCache->SetFileWatcher(useFileWatcher);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Automatically recompile shaders on file change. "
			"Intended for developing.");
	}

	// Debug addresses section (moved from Advanced/Logging)
	if (ImGui::TreeNodeEx("Addresses")) {
		auto Renderer = globals::game::renderer;
		auto BSShaderAccumulator = *globals::game::currentAccumulator.get();
		auto RendererShadowState = globals::game::shadowState;
		ADDRESS_NODE(Renderer)
		ADDRESS_NODE(BSShaderAccumulator)
		ADDRESS_NODE(RendererShadowState)
		ImGui::TreePop();
	}

	// Statistics section (moved from Advanced/Logging)
	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Shader Compiler : {}", shaderCache->GetShaderStatsString()).c_str());
		ImGui::TreePop();
	}

	// Frame annotations toggle (moved from Advanced/Logging)
	ImGui::Checkbox("Frame Annotations", &globals::state->frameAnnotations);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable detailed frame annotations for debugging render passes and draw calls.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Developer Mode Testing Section
	if (globals::state->IsDeveloperMode()) {
		FeatureIssues::Test::DrawDeveloperModeTestingUI();

		ImGui::Spacing();
		// Test Conditions button - runs a set of console commands to prepare the player for testing
		if (ImGui::Button("Test Conditions", { -1, 0 })) {
			if (auto ui = RE::UI::GetSingleton(); ui && !ui->menuStack.empty() && RE::PlayerCharacter::GetSingleton()) {
				RE::Console::ExecuteCommand("player.setav speedmult 1000");
				RE::Console::ExecuteCommand("tgm");
				RE::Console::ExecuteCommand("tcl");
				RE::Console::ExecuteCommand("set timescale to 0");
				RE::Console::ExecuteCommand("set gamehour to 12");
				RE::Console::ExecuteCommand("coc whiterun");
				RE::Console::ExecuteCommand("fw 81a");
			}
		}
	}
}

void AdvancedSettingsRenderer::RenderTestingSection()
{
	// A/B Testing settings
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->DrawSettingsUI();
}
