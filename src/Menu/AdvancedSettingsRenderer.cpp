#include "AdvancedSettingsRenderer.h"
#include "CSEditor/Browser/BrowserWidgets.h"

#include <algorithm>
#include <format>
#include <functional>
#include <future>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <thread>

#include "CSEditor/EditorWindow.h"
#include "FeatureIssues.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuIcons.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/Format.h"
#include "Utils/UI.h"

namespace
{
	namespace Ids = SettingsSectionIds;

	// Setting labels the Settings page search matches, per section; keep in step with the widgets below
	constexpr SettingsSearchEntry kSearchIndex[] = {
		// Shaders & Cache (compiler controls)
		{ Ids::kShadersCache, "menu.advanced.compiler_threads", "Compiler Threads" },
		{ Ids::kShadersCache, "menu.advanced.background_compiler_threads", "Background Compiler Threads" },
		{ Ids::kShadersCache, "menu.advanced.dump_shaders", "Dump Shaders" },
		{ Ids::kShadersCache, "menu.advanced.clear_shader_cache", "Clear Shader Cache" },
		// Developer
		{ Ids::kDeveloper, "menu.advanced.enable_developer_mode", "Enable Developer Mode" },
		{ Ids::kDeveloper, "menu.advanced.enable_file_watcher", "Enable File Watcher" },
		{ Ids::kDeveloper, "menu.advanced.dump_feature_defaults", "Dump All Feature Defaults to JSON" },
		{ Ids::kDeveloper, "menu.advanced.statistics", "Statistics" },
		{ Ids::kDeveloper, "menu.advanced.frame_annotations", "Frame Annotations" },
		{ Ids::kDeveloper, "menu.advanced.half_precision", "Half Precision (Partial Precision)" },
		{ Ids::kDeveloper, "menu.advanced.content_store", "Persistent Shader Store (Developers/Testers)" },
		{ Ids::kDeveloper, "menu.advanced.content_store_limit", "Store Size Limit" },
		{ Ids::kDeveloper, "menu.advanced.avoid_flow_control", "Avoid Flow Control" },
		// Logging
		{ Ids::kLogging, "menu.advanced.log_level", "Log Level" },
		{ Ids::kLogging, "menu.advanced.shader_defines", "Shader Defines" },
		{ Ids::kLogging, "menu.advanced.dump_ini_settings", "Dump Ini Settings" },
		{ Ids::kLogging, "menu.advanced.open_logs", "Open Logs" },
		// Shader Debug
		{ Ids::kShaderDebug, "menu.advanced.replace_original_shaders", "Replace Original Shaders" },
		{ Ids::kShaderDebug, "menu.advanced.enable_shader_blocking", "Enable Shader Blocking" },
		{ Ids::kActiveShaders, "menu.advanced.active_shaders", "Active Shaders" },
		// Testing (the A/B testing label is not translated)
		{ Ids::kTesting, nullptr, "A/B Test Interval" },
		{ Ids::kTesting, "menu.advanced.test_conditions", "Test Conditions" },
		// Disable at Boot
		{ Ids::kDisableAtBoot, "menu.settings.disabled_only", "Disabled only" },
	};

	/** @brief Clipboard text describing an active shader: type, class, descriptor, key and cache path. */
	std::string DescribeShader(const SIE::ShaderCache::ActiveShaderInfo& shader)
	{
		std::string diskPath;
		diskPath.reserve(shader.diskPath.size());
		for (wchar_t wc : shader.diskPath)
			diskPath += static_cast<char>(wc);

		return std::format("Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\nCache Path: {}",
			magic_enum::enum_name(shader.shaderType).data(),
			magic_enum::enum_name(shader.shaderClass).data(),
			shader.descriptor,
			shader.key,
			diskPath);
	}

	constexpr double kStoreUsageRefreshSeconds = 5.0;
	constexpr double kBytesPerMB = 1024.0 * 1024.0;

	/// Location, size and restore count of the persistent shader store, with a size limit and a button to empty it.
	void DrawContentStoreDetails(SIE::ShaderCache* a_cache, bool a_enabled)
	{
		// Measuring walks the whole store, so it runs on a worker every few seconds instead of in the frame.
		static SIE::ShaderCache::ContentStoreUsage usage;
		static std::future<SIE::ShaderCache::ContentStoreUsage> pendingUsage;
		static double lastRefresh = -kStoreUsageRefreshSeconds;
		const double now = ImGui::GetTime();
		if (pendingUsage.valid() && pendingUsage.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			usage = pendingUsage.get();
		if (!pendingUsage.valid() && now - lastRefresh >= kStoreUsageRefreshSeconds) {
			pendingUsage = std::async(std::launch::async, [a_cache] { return a_cache->GetContentStoreUsage(); });
			lastRefresh = now;
		}
		if (!a_enabled && usage.blobs == 0)
			return;

		ImGui::Indent();
		ImGui::TextDisabled("%s", I18n::GetSingleton()->Format("menu.advanced.content_store_location", { { "path", usage.path } }, "Location: {path}").c_str());
		ImGui::TextDisabled("%s", I18n::GetSingleton()->Format("menu.advanced.content_store_usage",
														  { { "count", std::to_string(usage.blobs) },
															  { "size", std::format("{:.0f}", static_cast<double>(usage.bytes) / kBytesPerMB) },
															  { "cap", std::format("{:.0f}", static_cast<double>(usage.maxBytes) / kBytesPerMB) } },
														  "Stored: {count} shaders, {size} MB (limit {cap} MB)")
									  .c_str());
		if (a_enabled)
			ImGui::TextDisabled("%s", I18n::GetSingleton()->Format("menu.advanced.content_store_hits", { { "count", std::to_string(a_cache->GetContentStoreHitTasks()) } }, "Restored since the last cache clear: {count}").c_str());

		auto limitMB = static_cast<int>(globals::state->contentStoreMaxMB.load(std::memory_order_relaxed));
		if (ImGui::SliderInt(T("menu.advanced.content_store_limit", "Store Size Limit"), &limitMB,
				static_cast<int>(State::kContentStoreMinMB), static_cast<int>(State::kContentStoreMaxMB), "%d MB", ImGuiSliderFlags_AlwaysClamp))
			globals::state->contentStoreMaxMB.store(static_cast<uint32_t>(limitMB), std::memory_order_relaxed);
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			a_cache->ApplyContentStoreLimit();
			lastRefresh = -kStoreUsageRefreshSeconds;
		}
		Util::HoverTip(T("menu.advanced.content_store_limit_tooltip",
			"Least recently used shaders are deleted once the store exceeds this. "
			"A full build stores about 120 MB, or about 1.1 GB in Developer Mode, "
			"so keep several builds' worth if you switch branches."));
		if (Util::Button(T("menu.advanced.content_store_clear", "Clear Store"))) {
			a_cache->ClearContentStore();
			lastRefresh = -kStoreUsageRefreshSeconds;
		}
		Util::HoverTip(T("menu.advanced.content_store_clear_tooltip",
			"Deletes every stored shader. Use this if a shader looks wrong after being restored; "
			"the next compile rebuilds the store from scratch. Shaders already in the shader cache are kept."));
		ImGui::Unindent();
	}
}

std::span<const SettingsSearchEntry> AdvancedSettingsRenderer::GetSearchIndex()
{
	return kSearchIndex;
}

void AdvancedSettingsRenderer::RenderCompilerSection()
{
	auto shaderCache = globals::shaderCache;

	{
		MenuFonts::FontRoleGuard headingFont(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.settings.section_compiler", "Compiler"));
	}

	// Compiler Thread controls
	ImGui::SliderInt(T("menu.advanced.compiler_threads", "Compiler Threads"), &shaderCache->compilationThreadCount, 1, static_cast<int32_t>(std::thread::hardware_concurrency()), "%d", ImGuiSliderFlags_AlwaysClamp);
	Util::HoverTip(T("menu.advanced.compiler_threads_tooltip",
		"Number of threads used to compile shaders at startup. "
		"Defaults to all logical cores minus one for OS headroom (E-cores included). "
		"Higher values finish compilation faster but may make the system less responsive."));
	ImGui::SliderInt(T("menu.advanced.background_compiler_threads", "Background Compiler Threads"), &shaderCache->backgroundCompilationThreadCount, 1, static_cast<int32_t>(std::thread::hardware_concurrency()), "%d", ImGuiSliderFlags_AlwaysClamp);
	Util::HoverTip(T("menu.advanced.background_compiler_threads_tooltip",
		"Number of threads used to compile shaders during gameplay. "
		"Defaults to half of performance cores to avoid impacting the render thread. "
		"Higher values finish compilation faster but may cause stuttering."));

	// Dump Shaders option
	bool useDump = shaderCache->IsDump();
	if (ImGui::Checkbox(T("menu.advanced.dump_shaders", "Dump Shaders"), &useDump)) {
		shaderCache->SetDump(useDump);
	}
	Util::HoverTip(T("menu.advanced.dump_shaders_tooltip", "Dump shaders at startup. This should be used only when reversing shaders. Normal users don't need this."));

	// Clear Shader Cache button
	if (Util::Button(T("menu.advanced.clear_shader_cache", "Clear Shader Cache"), { -1, 0 })) {
		shaderCache->Clear();
	}
	Util::HoverTip(T("menu.advanced.clear_shader_cache_tooltip", "Clear all compiled shaders from memory. Forces recompilation of all shaders on next use."));
}

void AdvancedSettingsRenderer::RenderLoggingSection()
{
	auto shaderCache = globals::shaderCache;

	// Log Level selection
	spdlog::level::level_enum logLevel = globals::state->GetLogLevel();
	const char* items[] = {
		T("menu.advanced.log_level_trace", "trace"),
		T("menu.advanced.log_level_debug", "debug"),
		T("menu.advanced.log_level_info", "info"),
		T("menu.advanced.log_level_warn", "warn"),
		T("menu.advanced.log_level_err", "err"),
		T("menu.advanced.log_level_critical", "critical"),
		T("menu.advanced.log_level_off", "off")
	};
	int item_current = static_cast<int>(logLevel);
	if (ImGui::Combo(T("menu.advanced.log_level", "Log Level"), &item_current, items, IM_ARRAYSIZE(items))) {
		ImGui::SameLine();
		globals::state->SetLogLevel(static_cast<spdlog::level::level_enum>(item_current));
	}
	Util::HoverTip(T("menu.advanced.log_level_tooltip", "Log level. Trace is most verbose. Default is info. Debug and Trace also enable Developer Mode."));

	// Shader Defines input
	// SetDefines normalizes the string, so the buffer is only reloaded while the input is idle.
	static std::string shaderDefines;
	static bool editingDefines = false;
	if (!editingDefines)
		shaderDefines = globals::state->GetDefinesString();
	ImGui::InputText(T("menu.advanced.shader_defines", "Shader Defines"), &shaderDefines);
	editingDefines = ImGui::IsItemActive();
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		globals::state->SetDefines(shaderDefines);
		shaderCache->Clear();
	}
	Util::HoverTip(T("menu.advanced.shader_defines_tooltip", "Defines for Shader Compiler. Semicolon \";\" separated. Clear with space. Rebuild shaders after making change. Compute Shaders require a restart to recompile."));

	ImGui::Spacing();

	if (Icons::LabeledButton("##DumpIni", Icons::TI(ICON_TI_FILE_EXPORT), T("menu.advanced.dump_ini_settings", "Dump Ini Settings")))
		Util::DumpSettingsOptions();
	if (const auto logPath = Util::PathHelpers::GetLogPath(); !logPath.empty()) {
		ImGui::SameLine();
		if (Icons::LabeledButton("##OpenLogs", Icons::TI(ICON_TI_FOLDER_OPEN), T("menu.advanced.open_logs", "Open Logs")))
			ShellExecuteA(NULL, "open", logPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
	}
}

void AdvancedSettingsRenderer::RenderShaderDebugSection()
{
	auto state = globals::state;

	Util::DrawSectionHeader(T("menu.advanced.replace_original_shaders", "Replace Original Shaders"));

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
		ImGui::EndTable();
	}

	if (!state->IsDeveloperMode())
		return;

	ImGui::Spacing();
	Util::DrawSectionHeader(T("menu.advanced.replace_shader_stages", "Replace Shader Stages"));
	if (ImGui::BeginTable("##ReplaceStages", 3, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		ImGui::Checkbox(T("menu.advanced.vertex", "Vertex"), &state->enableVShaders);
		Util::HoverTip(T("menu.advanced.vertex_tooltip",
			"Replace Vertex Shaders. "
			"When false, will disable the custom Vertex Shaders for the types above. "
			"For developers to test whether CS shaders match vanilla behavior. "));

		ImGui::TableNextColumn();
		ImGui::Checkbox(T("menu.advanced.pixel", "Pixel"), &state->enablePShaders);
		Util::HoverTip(T("menu.advanced.pixel_tooltip",
			"Replace Pixel Shaders. "
			"When false, will disable the custom Pixel Shaders for the types above. "
			"For developers to test whether CS shaders match vanilla behavior. "));

		ImGui::TableNextColumn();
		ImGui::Checkbox(T("menu.advanced.compute", "Compute"), &state->enableCShaders);
		Util::HoverTip(T("menu.advanced.compute_tooltip",
			"Replace Compute Shaders. "
			"When false, will disable the custom Compute Shaders for the types above. "
			"For developers to test whether CS shaders match vanilla behavior. "));
		ImGui::EndTable();
	}

	ImGui::Spacing();
	auto& menuSettings = globals::menu->GetSettings();
	Util::CheckboxTip(T("menu.advanced.enable_shader_blocking", "Enable Shader Blocking"), &menuSettings.EnableShaderBlocking, T("menu.advanced.enable_shader_blocking_tooltip", "Enables hotkeys to cycle through and block individual shaders for debugging purposes."));
	if (menuSettings.EnableShaderBlocking) {
		ImGui::Indent();
		ImGui::TextDisabled("%s", T("menu.advanced.shader_block_keys_moved", "Set the Block Previous and Block Next keys under Keybindings."));
		ImGui::Unindent();
	}
}

void AdvancedSettingsRenderer::RenderActiveShadersSection()
{
	auto shaderCache = globals::shaderCache;
	if (!globals::state->IsDeveloperMode()) {
		ImGui::TextDisabled("%s", T("menu.advanced.active_shaders_needs_dev", "Enable Developer Mode to list the active shaders."));
		return;
	}

	if (!shaderCache->blockedKey.empty()) {
		// Create a visually distinct box for the blocked shader info with rounded corners and border
		const float scale = Util::GetUIScale();
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ThemeManager::Constants::BLOCKED_SHADER_BOX_ROUNDING * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, ImGui::GetStyle().WindowBorderSize);
		ImVec4 blockedBgColor = Util::Colors::GetError();
		blockedBgColor.w = ThemeManager::Constants::BLOCKED_SHADER_BOX_BG_ALPHA;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, blockedBgColor);

		float maxHeight = ImGui::GetContentRegionAvail().y * ThemeManager::Constants::BLOCKED_SHADER_BOX_MAX_HEIGHT_RATIO;
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, maxHeight));
		if (ImGui::BeginChild("##BlockedShaderInfo", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
			Util::Text::Error(T("menu.advanced.shader_blocking_active", "Shader Blocking Active"));
			ImGui::SameLine();
			if (ImGui::SmallButton(T("menu.advanced.stop_blocking", "Stop Blocking##Section"))) {
				shaderCache->DisableShaderBlocking();
			}

			ImGui::Text(T("menu.advanced.blocked_shader", "Blocked: %s"), shaderCache->blockedKey.c_str());

			// Try to get more details from active shaders
			auto activeShaders = shaderCache->GetActiveShaders();
			for (const auto& shader : activeShaders) {
				if (shader.key == shaderCache->blockedKey) {
					ImGui::Text(T("menu.advanced.shader_type_label", "Type: %s"), magic_enum::enum_name(shader.shaderType).data());
					ImGui::Text(T("menu.advanced.shader_class_label", "Class: %s"), magic_enum::enum_name(shader.shaderClass).data());
					ImGui::Text(T("menu.advanced.shader_descriptor", "Descriptor: 0x%X"), shader.descriptor);

					// Add button to copy shader info to clipboard
					ImGui::PushID(shader.key.c_str());
					auto copyInfoLabel = std::format("{}##BlockedShader", T("menu.advanced.copy_info", "Copy Info"));
					if (ImGui::SmallButton(copyInfoLabel.c_str())) {
						ImGui::SetClipboardText(DescribeShader(shader).c_str());
					}
					ImGui::PopID();
					if (ImGui::IsItemHovered()) {
						Util::HoverTip(T("menu.advanced.copy_info_tooltip", "Copy complete shader information including cache path to clipboard"));
					}

					break;
				}
			}
		}
		ImGui::EndChild();

		ImGui::PopStyleVar(2);   // ChildRounding, ChildBorderSize
		ImGui::PopStyleColor();  // ChildBg
	}

	ImGui::Spacing();
	// Get fresh active shaders data for accurate count and table
	auto activeShaders = shaderCache->GetActiveShaders();
	uint32_t totalDrawCalls = 0;
	for (const auto& shader : activeShaders) {
		totalDrawCalls += shader.drawCalls;
	}
	ImGui::TextDisabled("%s", I18n::GetSingleton()->Format("menu.advanced.active_shaders_count",
													  { { "count", std::to_string(activeShaders.size()) }, { "draws", std::to_string(totalDrawCalls) } },
													  "{count} shaders, {draws} draw calls this frame")
								  .c_str());

	// Static variables to maintain table filter state
	static char filterText[256] = "";
	static int searchColumn = 0;        // 0 = All Columns, then one per column: Type, Class, Descriptor, Frame %, Key
	static size_t sortColumn = 3;       // Default sort by Frame % (draw calls)
	static bool sortAscending = false;  // Descending by default (highest usage first)

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
		{ T("menu.advanced.column_type", "Type"), T("menu.advanced.column_type_tooltip", "Shader type"), [](const ShaderRow& row) {
			 return std::string(magic_enum::enum_name(row.shader.shaderType));
		 } },
		{ T("menu.advanced.column_class", "Class"), T("menu.advanced.column_class_tooltip", "Shader class"), [](const ShaderRow& row) {
			 return std::string(magic_enum::enum_name(row.shader.shaderClass));
		 } },
		{ T("menu.advanced.column_descriptor", "Descriptor"), T("menu.advanced.column_descriptor_tooltip", "Shader descriptor"), [](const ShaderRow& row) {
			 return std::format("0x{:X}", row.shader.descriptor);
		 } },
		{ T("menu.advanced.column_frame_pct", "Frame %"), T("menu.advanced.column_frame_pct_tooltip", "Percentage of draw calls this frame"), [](const ShaderRow& row) {
			 float percentage = Util::CalculatePercentage(static_cast<float>(row.shader.drawCalls), static_cast<float>(row.totalDrawCalls));
			 return Util::FormatPercent(percentage);
		 } },
		{ T("menu.advanced.column_key", "Key"), T("menu.advanced.column_key_tooltip", "Shader key"), [](const ShaderRow& row) {
			 return row.shader.key;
		 } }
	};

	// Row click callbacks
	auto onRowLeftClick = [shaderCache](const ShaderRow& row) {
		if (row.shader.key == shaderCache->blockedKey) {
			shaderCache->DisableShaderBlocking();
		} else {
			shaderCache->blockedKey = row.shader.key;
			logger::info("Blocking shader: {}", row.shader.key);
		}
	};

	auto onRowRightClick = [shaderCache](const ShaderRow& row) {
		ImGui::SetClipboardText(DescribeShader(row.shader).c_str());
	};
	auto getRowTooltip = [shaderCache](const ShaderRow& row) {
		std::string clickAction = (row.shader.key == shaderCache->blockedKey) ? T("menu.advanced.click_to_unblock", "Left-click to unblock this shader") : T("menu.advanced.click_to_block", "Left-click to block this shader");
		auto shaderType = magic_enum::enum_name(row.shader.shaderType);
		auto shaderClass = magic_enum::enum_name(row.shader.shaderClass);

		return std::vformat(T("menu.advanced.shader_row_tooltip", "Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\n\n{}"), std::make_format_args(
																																 shaderType,
																																 shaderClass,
																																 row.shader.descriptor,
																																 row.shader.key,
																																 clickAction));
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
		{ Util::TableInputEventType::ContextMenu, onRowRightClick, T("menu.advanced.copy_info", "Copy Info"), 1 }
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
		getRowTooltip,
		nullptr,
		nullptr,
		0.0f);

	// Update static variables with modified filter state
	strncpy_s(filterText, filterState.filterText.c_str(), sizeof(filterText) - 1);
	filterText[sizeof(filterText) - 1] = '\0';
	searchColumn = filterState.searchColumn;
}

void AdvancedSettingsRenderer::RenderDeveloperSection()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!ImGui::BeginTable("##DeveloperLayout", 2, ImGuiTableFlags_SizingStretchSame))
		return;
	ImGui::TableNextColumn();
	if (ImGui::Checkbox(T("menu.advanced.enable_developer_mode", "Enable Developer Mode"), &state->enableDeveloperMode)) {
		logger::info("Developer Mode {}", state->enableDeveloperMode ? "enabled" : "disabled");
	}
	Util::HoverTip(T("menu.advanced.enable_developer_mode_tooltip",
		"Unlocks developer-only options and tooling. "
		"Also enabled automatically when Log Level is debug or trace. "
		"Use at your own risk."));
	if (!state->enableDeveloperMode && state->GetLogLevel() <= spdlog::level::debug) {
		ImGui::TextDisabled("%s", T("menu.advanced.developer_mode_via_log_level",
									  "Currently active because Log Level is debug/trace."));
	}

	ImGui::Spacing();

	bool useFileWatcher = shaderCache->UseFileWatcher();
	if (ImGui::Checkbox(T("menu.advanced.enable_file_watcher", "Enable File Watcher"), &useFileWatcher)) {
		shaderCache->SetFileWatcher(useFileWatcher);
	}
	Util::HoverTip(T("menu.advanced.enable_file_watcher_tooltip",
		"Automatically recompile shaders on file change. "
		"Intended for developing."));

	if (Icons::LabeledButton("##DumpDefaults", Icons::TI(ICON_TI_FILE_EXPORT), T("menu.advanced.dump_feature_defaults", "Dump All Feature Defaults to JSON"))) {
		if (const auto path = state->DumpFeatureDefaults(); !path.empty())
			Util::FileHelpers::OpenWithShell(path);
	}
	Util::HoverTip(T("menu.advanced.dump_feature_defaults_tooltip",
		"Writes every feature's built-in default settings to FeatureDefaults.json and opens it. "
		"Defaults come from the running build; Baseline packs and your edits are not included."));

	// The shader compiler flags share a card on the right
	ImGui::TableNextColumn();
	if (BrowserUI::BeginCard("##CompilerFlags", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY)) {
		ImGui::SeparatorText(T("menu.advanced.compiler_flags", "Shader Compiler Flags"));
		Util::CheckboxTip(T("menu.advanced.frame_annotations", "Frame Annotations"), &globals::state->frameAnnotations, T("menu.advanced.frame_annotations_tooltip", "Enable detailed frame annotations for debugging render passes and draw calls."));

		// Half-precision (partial precision) shader compile flag
		bool partialPrecision = globals::state->enablePartialPrecision.load(std::memory_order_relaxed);
		if (ImGui::Checkbox(T("menu.advanced.half_precision", "Half Precision (Partial Precision)"), &partialPrecision)) {
			globals::state->enablePartialPrecision.store(partialPrecision, std::memory_order_relaxed);
			// Force a recompile so the flag actually takes effect on subsequent shader builds.
			globals::shaderCache->Clear();
		}
		Util::HoverTip(T("menu.advanced.half_precision_tooltip",
			"Adds D3DCOMPILE_PARTIAL_PRECISION to the shader compiler flags.\n"
			"Lets fxc downgrade unmarked float ops to FP16 where it can prove safety, "
			"on top of the existing min16float type hints.\n"
			"On FP16-capable GPUs (Pascal+ / GCN+ / Skylake+) this can halve register "
			"pressure and double ALU throughput, but it can also introduce minor visual "
			"differences in shaders that haven't been audited for precision sensitivity.\n"
			"Toggling this clears the shader cache and triggers a full recompile."));

		bool contentStore = globals::state->enableContentStore.load(std::memory_order_relaxed);
		if (ImGui::Checkbox(T("menu.advanced.content_store", "Persistent Shader Store (Developers/Testers)"), &contentStore))
			globals::state->enableContentStore.store(contentStore, std::memory_order_relaxed);
		Util::HoverTip(T("menu.advanced.content_store_tooltip",
			"Keeps every compiled shader keyed by its preprocessed source and compiler flags. "
			"After a cache wipe, a plugin update or a feature change, shaders already compiled "
			"for the same code are restored instead of recompiled.\n"
			"Costs one preprocess per shader (tens of milliseconds) and disk space in "
			"Data/ShaderCache/ContentStore, trimmed to the least recently used. "
			"Takes effect for shaders compiled from now on."));
		DrawContentStoreDetails(globals::shaderCache, contentStore);

		// Avoid flow control compiler flag (transient — not saved to config because the
		// right setting depends on the current scene, not the user).
		bool avoidFlowControl = globals::state->enableAvoidFlowControl.load(std::memory_order_relaxed);
		if (ImGui::Checkbox(T("menu.advanced.avoid_flow_control", "Avoid Flow Control"), &avoidFlowControl)) {
			globals::state->enableAvoidFlowControl.store(avoidFlowControl, std::memory_order_relaxed);
			// Force a recompile so the flag actually takes effect on subsequent shader builds.
			globals::shaderCache->Clear();
		}
		Util::HoverTip(T("menu.advanced.avoid_flow_control_tooltip",
			"Adds D3DCOMPILE_AVOID_FLOW_CONTROL to the shader compiler flags.\n"
			"Forces fxc to flatten branches into predicated ops rather than emitting "
			"dynamic flow control. Often a win for short branch bodies and uniformly-"
			"taken branches; usually a loss for long divergent branches that vanilla "
			"flow control would skip entirely.\n"
			"Resets every launch. Toggling this clears the shader cache and triggers a "
			"full recompile."));
	}
	ImGui::EndChild();
	ImGui::EndTable();

	// Addresses and build statistics are niche, so they come last
	if (ImGui::TreeNodeEx(T("menu.advanced.addresses", "Addresses"))) {
		auto Renderer = globals::game::renderer;
		auto BSShaderAccumulator = *globals::game::currentAccumulator.get();
		auto RendererShadowState = globals::game::shadowState;
		ADDRESS_NODE(Renderer);
		ADDRESS_NODE(BSShaderAccumulator);
		ADDRESS_NODE(RendererShadowState);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T("menu.advanced.statistics", "Statistics"))) {
		std::string shaderStatsString = shaderCache->GetShaderStatsString();
		ImGui::Text("%s", std::vformat(T("menu.advanced.shader_compiler_stats", "Shader Compiler : {}"), std::make_format_args(shaderStatsString)).c_str());

		// Derived parallelism metrics are computed lazily on demand and only shown
		// once compilation has completed to avoid per-frame analysis while compiling.
		if (!shaderCache->IsCompiling()) {
			auto parallelism = shaderCache->GetParallelismStats();
			if (parallelism.has_value()) {
				const auto& p = parallelism.value();
				ImGui::Spacing();
				ImGui::TextDisabled(T("menu.advanced.parallelism_header", "Parallelism (derived from %zu compiled tasks)"), p.sampleCount);
				Util::HoverTip(T("menu.advanced.parallelism_tooltip_1", "Computed lazily from the last completed build."));
				ImGui::Text("%s", T("menu.advanced.parallelism_tooltip_2", "Only evaluated when this Statistics section is open."));
				ImGui::Text(T("menu.advanced.work_metric", "Work (W, sum of task wall times): %s"), Util::FormatDuration(p.workMs).c_str());
				Util::HoverTip(T("menu.advanced.work_tooltip_1", "Total compile work: sum of all per-shader wall-clock compile times."));
				ImGui::Text("%s", T("menu.advanced.work_tooltip_2", "This is not CPU time; it is accumulated task elapsed time."));
				ImGui::Text("%s", T("menu.advanced.work_tooltip_3", "Equivalent serial time on one worker if overhead stayed the same."));
				ImGui::Text(T("menu.advanced.span_metric", "Span (S, longest): %s"), Util::FormatDuration(p.spanMs).c_str());
				Util::HoverTip(T("menu.advanced.span_tooltip_1", "Critical-path lower bound, approximated by the single slowest shader."));
				ImGui::Text("%s", T("menu.advanced.span_tooltip_2", "Even infinite cores cannot finish faster than this."));
				ImGui::Text(T("menu.advanced.makespan_metric", "Makespan (T_p): %s"), Util::FormatDuration(p.makespanMs).c_str());
				Util::HoverTip(T("menu.advanced.makespan_tooltip", "Observed wall-clock duration for the full shader build."));
				ImGui::Text(T("menu.advanced.queue_wait_metric", "Queue wait (avg/max): %s / %s"),
					Util::FormatDuration(p.avgQueueWaitMs).c_str(),
					Util::FormatDuration(p.maxQueueWaitMs).c_str());
				Util::HoverTip(T("menu.advanced.queue_wait_tooltip_1", "Time spent waiting in the ready queue before a worker started compilation."));
				ImGui::Text("%s", T("menu.advanced.queue_wait_tooltip_2", "Useful for identifying scheduler-induced delay separate from compile cost."));
				ImGui::Text(T("menu.advanced.avg_parallelism_metric", "Average parallelism (W/S): %.2fx"), p.avgParallelism);
				Util::HoverTip(T("menu.advanced.avg_parallelism_tooltip_1", "Average useful concurrency in this workload."));
				ImGui::Text("%s", T("menu.advanced.avg_parallelism_tooltip_2", "Roughly the worker count where adding more cores gives diminishing returns."));
				ImGui::Text(T("menu.advanced.infinite_core_efficiency_metric", "Infinite-core efficiency (S/T_p): %.1f%%"), 100.0 * p.infiniteCoreEfficiency);
				Util::HoverTip(T("menu.advanced.infinite_core_efficiency_tooltip_1", "How close runtime is to the infinite-core lower bound."));
				// Passed as the format on purpose: the translated strings carry an escaped percent sign
				ImGui::TextUnformatted(T("menu.advanced.infinite_core_efficiency_tooltip_2", "100% means T_p == S."));
				ImGui::Text(T("menu.advanced.infinite_core_gap_metric", "Infinite-core gap: %.1f%%"), p.infiniteCoreGapPercent);
				Util::HoverTip(T("menu.advanced.infinite_core_gap_tooltip_1", "Distance from ideal infinite-core time."));
				ImGui::Text("%s", T("menu.advanced.infinite_core_gap_tooltip_2", "Defined as 100 * (1 - S / T_p). Lower is better."));

				ImGui::Spacing();
				ImGui::TextDisabled("%s", T("menu.advanced.infinite_core_efficiency", "Infinite-core efficiency"));
				float efficiency = static_cast<float>(std::clamp(p.infiniteCoreEfficiency, 0.0, 1.0));
				double efficiencyPercent = 100.0 * p.infiniteCoreEfficiency;
				std::string efficiencyProgress = std::vformat(T("menu.advanced.efficiency_progress", "{:.1f}% efficient / {:.1f}% gap"), std::make_format_args(efficiencyPercent, p.infiniteCoreGapPercent));
				ImGui::ProgressBar(efficiency, ImVec2(-1.0f, 0.0f), efficiencyProgress.c_str());

				ImGui::Spacing();
				ImGui::TextDisabled("%s", T("menu.advanced.relative_durations", "Relative durations (normalized)"));
				double maxMs = std::max({ p.workMs, p.spanMs, p.makespanMs, 1.0 });
				auto drawRelativeBar = [maxMs](const char* label, double value) {
					float ratio = static_cast<float>(std::clamp(value / maxMs, 0.0, 1.0));
					ImGui::TextUnformatted(label);
					ImGui::SameLine();
					std::string duration = Util::FormatDuration(value);
					double percent = 100.0 * ratio;
					std::string progressText = std::vformat(T("menu.advanced.relative_bar_format", "{} ({:.1f}%)"), std::make_format_args(duration, percent));
					ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), progressText.c_str());
				};
				drawRelativeBar(T("menu.advanced.span_label", "Span (S)"), p.spanMs);
				drawRelativeBar(T("menu.advanced.makespan_label", "Makespan (T_p)"), p.makespanMs);
				drawRelativeBar(T("menu.advanced.work_label", "Work (W)"), p.workMs);
			}
		}

		// Top-3 slowest shaders from the last build
		auto topSlow = shaderCache->GetTopSlowTasks(3);
		if (!topSlow.empty()) {
			ImGui::Spacing();
			ImGui::TextDisabled(T("menu.advanced.top_slowest_shaders", "Top %zu Slowest Shaders (last build)"), topSlow.size());
			for (size_t i = 0; i < topSlow.size(); ++i) {
				const auto& rec = topSlow[i];
				ImGui::Text(T("menu.advanced.shader_slow_entry", "#%zu  %s  (weight %d)"), i + 1,
					Util::FormatDuration(rec.elapsedMs).c_str(), rec.priority);
				ImGui::SameLine();
				ImGui::TextDisabled("%s", rec.key.c_str());
				if (ImGui::IsItemHovered()) {
					Util::HoverTip(rec.key.c_str());
				}
				// Allow copying the full key with a right-click
				if (ImGui::BeginPopupContextItem(std::format("##slowcopy{}", i).c_str())) {
					if (ImGui::MenuItem(T("menu.advanced.copy_key", "Copy key"))) {
						ImGui::SetClipboardText(rec.key.c_str());
					}
					ImGui::EndPopup();
				}
			}
		}

		ImGui::TreePop();
	}
}

void AdvancedSettingsRenderer::RunTestConditions()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui || ui->menuStack.empty() || !RE::PlayerCharacter::GetSingleton())
		return;
	RE::Console::ExecuteCommand("player.setav speedmult 1000");
	RE::Console::ExecuteCommand("tgm");
	RE::Console::ExecuteCommand("tcl");
	EditorWindow::GetSingleton()->PauseTime();
	RE::Console::ExecuteCommand("set gamehour to 12");
	RE::Console::ExecuteCommand("coc whiterun");
	RE::Console::ExecuteCommand("fw 81a");
}

const char* AdvancedSettingsRenderer::GetTestConditionsTooltip()
{
	return T("menu.advanced.test_conditions_tooltip",
		"Sets up the player for testing by running these console commands:\n"
		"  player.setav speedmult 1000 - move very fast\n"
		"  tgm - god mode\n"
		"  tcl - no collision (fly through the world)\n"
		"  set gamehour to 12 - noon, with time paused\n"
		"  coc whiterun - teleport to Whiterun\n"
		"  fw 81a - force weather 81A\n"
		"Only runs while a game is loaded and a menu is open.");
}

void AdvancedSettingsRenderer::RenderTestingSection()
{
	// A/B Testing settings
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->DrawSettingsUI();

	// Developer mode: the issue tests and the one-click test scene
	if (globals::state->IsDeveloperMode()) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		FeatureIssues::Test::DrawDeveloperModeTestingUI();

		ImGui::Spacing();
		if (Icons::LabeledButton("##TestConditions", Icons::TI(MenuIcons::kTestScene), T("menu.advanced.test_conditions", "Test Conditions")))
			RunTestConditions();
		Util::AddTooltip(GetTestConditionsTooltip());
	}
}
