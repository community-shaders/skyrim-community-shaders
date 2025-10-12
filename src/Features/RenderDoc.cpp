// RenderDoc feature implementation providing in-application graphics debugging capabilities
#include "Features/RenderDoc.h"

#include "Globals.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
// Include additional core headers required by the feature implementation
#include "Menu.h"
#include "Plugin.h"
#include "State.h"
#include "Utils/UI.h"
#include <imgui.h>

// Include the real RenderDoc API and Windows headers only in the implementation
#include <Renderdoc/renderdoc_app.h>
#include <algorithm>
#include <chrono>
#include <dxgi.h>
#include <filesystem>
#include <format>
#include <shellapi.h>
#include <thread>
#include <vector>
#include <windows.h>

// Global feature instance implementation
RenderDoc* RenderDoc::GetSingleton()
{
	return &globals::features::renderDoc;
}

void RenderDoc::Load()
{
	// Only load RenderDoc if the user has enabled capture
	if (!enableRenderDocCapture) {
		logger::debug("[RenderDoc] RenderDoc capture disabled, skipping initialization");
		return;
	}

	// Load RenderDoc only from our expected location. Do not fall back to system PATH.
	std::filesystem::path renderdocPath = GetRenderDocDllPath();
	if (renderdocPath.empty() || !std::filesystem::exists(renderdocPath)) {
		logger::debug("[RenderDoc] renderdoc.dll not found at expected path: {}", renderdocPath.string());
		return;
	}

	std::wstring widePath = renderdocPath.wstring();
	HMODULE moduleHandle = LoadLibraryW(widePath.c_str());
	logger::debug("[RenderDoc] Attempting to load renderdoc.dll from {}", renderdocPath.string());

	renderDocModule = moduleHandle;
	if (!renderDocModule) {
		logger::debug("[RenderDoc] Failed to load renderdoc.dll from {}", renderdocPath.string());
		return;
	}

	// Log RenderDoc DLL version if possible. Prefer the actual loaded module path
	try {
		std::wstring dllPathW;
		// If we had a computed path and it exists, prefer that; otherwise ask the OS for the loaded module filename
		if (!renderdocPath.empty() && std::filesystem::exists(renderdocPath)) {
			dllPathW = renderdocPath.wstring();
		} else {
			wchar_t buf[MAX_PATH]{ 0 };
			if (GetModuleFileNameW((HMODULE)renderDocModule, buf, (DWORD)std::size(buf)) != 0) {
				dllPathW = std::wstring(buf);
			}
		}

		if (!dllPathW.empty()) {
			auto ver = Util::GetDllVersion(dllPathW);
			if (ver.has_value()) {
				logger::info("[RenderDoc] Loaded renderdoc.dll version {} (from {})", Util::GetFormattedVersion(*ver), Util::WStringToString(dllPathW));
			} else {
				logger::info("[RenderDoc] Loaded renderdoc.dll (version unknown) from {}", Util::WStringToString(dllPathW));
			}
		} else {
			logger::info("[RenderDoc] Loaded renderdoc.dll (module path unknown)");
		}
	} catch (...) {
		logger::info("[RenderDoc] Loaded renderdoc.dll (version lookup failed)");
	}

	// Get the API function pointer
	auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress((HMODULE)renderDocModule, "RENDERDOC_GetAPI");
	if (!RENDERDOC_GetAPI) {
		logger::warn("[RenderDoc] Failed to get RENDERDOC_GetAPI function");
		FreeLibrary((HMODULE)renderDocModule);
		renderDocModule = nullptr;
		return;
	}

	// Get the API interface
	int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&renderDocApi);
	if (ret != 1 || !renderDocApi) {
		logger::warn("[RenderDoc] Failed to get API interface");
		FreeLibrary((HMODULE)renderDocModule);
		renderDocModule = nullptr;
		renderDocApi = nullptr;
		return;
	}

	// Build a base capture file path template including the Skyrim runtime and version so captures are easy to identify
	try {
		auto capturesDir = GetCapturesPath();
		Util::FileHelpers::EnsureDirectoryExists(capturesDir);

		// Format runtime + game version into filename base
		auto runtimeName = std::string{ magic_enum::enum_name(REL::Module::GetRuntime()) };
		auto gameVersion = Util::GetFormattedVersion(REL::Module::get().version());

		// Build the path using std::filesystem so we don't hardcode separators
		std::filesystem::path fileBase = capturesDir / std::format("Skyrim_{}_{}", runtimeName, gameVersion);
		renderDocApi->SetCaptureFilePathTemplate(fileBase.string().c_str());
	} catch (const std::exception& e) {
		logger::warn("[RenderDoc] Failed to prepare capture directory/template: {}", e.what());
	}

	renderDocApi->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);

	// Initialize capture count tracking
	lastCaptureCount = renderDocApi->GetNumCaptures();

	logger::info("[RenderDoc] Successfully initialized");
}

void RenderDoc::DrawSettings()
{
	// Track section visibility for intelligent cache refreshing
	bool isSectionVisible = false;

	// Include enable toggle and annotation forcing logic here
	bool prevRenderDocCapture = enableRenderDocCapture;
	if (ImGui::Checkbox("Enable RenderDoc Capture", &enableRenderDocCapture)) {
		if (enableRenderDocCapture && !prevRenderDocCapture) {
			globals::state->useFrameAnnotations = globals::state->frameAnnotations;
			globals::state->frameAnnotations = true;
		}
		if (!enableRenderDocCapture && prevRenderDocCapture) {
			globals::state->frameAnnotations = globals::state->useFrameAnnotations;
		}
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable RenderDoc frame capture for providing debug captures to the Community Shaders team.");
		ImGui::Text("Enabling capture will force-enable frame annotations for easier debugging and will restore the previous setting when disabled.");
	}

	// The rest of the UI renders only when capture is active
	bool renderDocCaptureEnabled = enableRenderDocCapture;
	bool renderDocActive = IsAvailable();

	const auto& themeSettings = Menu::GetSingleton()->GetTheme();

	if (renderDocCaptureEnabled && !renderDocActive) {
		ImGui::TextColored(themeSettings.StatusPalette.RestartNeeded, "Requires restart to enable RenderDoc capture.");
		return;
	}

	if (!renderDocCaptureEnabled && renderDocActive) {
		ImGui::TextColored(themeSettings.StatusPalette.Warning, "Requires restart to disable RenderDoc capture, performance will be severely impacted.");
		return;
	}

	if (renderDocCaptureEnabled && renderDocActive) {
		isSectionVisible = true;
		// Capture Control Section
		{
			auto captureSection = Util::SectionWrapper("Capture Control", "Manual capture creation and basic controls");
			if (captureSection) {
				ImGui::TextColored(themeSettings.StatusPalette.InfoColor, "RenderDoc capture is active.");
				ImGui::SameLine();

				std::string enabledFeaturesPreview;
				for (auto* feat : Feature::GetFeatureList()) {
					if (!feat->loaded)
						continue;

					std::string ver = feat->version.empty() ? std::string("<unknown>") : feat->version;
					if (!enabledFeaturesPreview.empty())
						enabledFeaturesPreview += '\n';
					enabledFeaturesPreview += std::format("- {} ({})", feat->GetShortName(), ver);
				}

				// Comments input for next capture
				static char commentsBuffer[kCommentsBufferSize] = { 0 };

				ImGui::InputTextWithHint("##CaptureComments", "Additional comments for next capture (optional)", commentsBuffer, sizeof(commentsBuffer));
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Additional comments will be appended to automatic metadata and embedded in the .rdc file");
				}

				if (ImGui::Button("Create Capture")) {
					// Check available disk space before allowing capture
					try {
						auto capturesDir = GetCapturesDirectory();
						std::error_code ec;
						uint64_t freeSpace = std::filesystem::space(capturesDir, ec).available;
						if (ec) {
							logger::warn("[RenderDoc] Failed to check available space in '{}': {}", capturesDir, ec.message());
							freeSpace = 0;
						}

						if (freeSpace < kMinCaptureSpaceBytes) {
							ImGui::OpenPopup("Not enough disk space##RenderDoc");
						} else {
							// Set comments if provided
							if (strlen(commentsBuffer) > 0) {
								// Build complete comments with automatic metadata plus user input
								std::string userComments = std::string(commentsBuffer);

								std::string completeComments = BuildAutomaticCaptureComments(userComments);

								SetPendingCaptureComments(completeComments);
								memset(commentsBuffer, 0, sizeof(commentsBuffer));  // Clear the buffer
							}

							// Actual capture logic
							logger::info("[RenderDoc] Manual capture triggered by user");
							TriggerCapture();

							// Note: Comments will be applied automatically when the capture finishes
							// via ApplyAutomaticCommentsToNewCaptures() called from RefreshCaptureFileCache()
						}
					} catch (const std::exception& e) {
						logger::error("[RenderDoc] Exception during capture logic: {}", e.what());
					}
				}

				if (ImGui::BeginPopup("Not enough disk space##RenderDoc")) {
					ImGui::Text("Not enough free disk space to create a capture.");
					ImGui::Text("At least {} MB of free space is required.", kMinCaptureSpaceBytes / (1024 * 1024));
					if (ImGui::Button("OK")) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}

				ImGui::SameLine();
				if (ImGui::Button("Open Capture Directory")) {
					// Open the directory where captures are saved
					try {
						auto capturesDir = GetCapturesDirectory();
						ShellExecuteA(nullptr, "open", capturesDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					} catch (const std::exception& e) {
						logger::error("[RenderDoc] Exception while trying to open captures directory: {}", e.what());
					}
				}

				ImGui::TextDisabled("Capture Directory: %s", GetCapturesDirectory().c_str());
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Right-click to copy the directory path.");
				}

				if (ImGui::BeginPopupContextItem()) {
					if (ImGui::MenuItem("Copy Directory Path")) {
						// Copy the captures directory path to clipboard
						try {
							auto capturesDir = GetCapturesDirectory();
							ImGui::SetClipboardText(capturesDir.c_str());
							logger::info("[RenderDoc] Copied captures directory path to clipboard: {}", capturesDir);
						} catch (const std::exception& e) {
							logger::error("[RenderDoc] Exception while copying directory path: {}", e.what());
						}
					}
					ImGui::EndPopup();
				}
			}
		}

		// Disk Usage Section
		{
			auto diskSection = Util::SectionWrapper("Disk Usage", "Monitor capture storage usage");
			if (diskSection) {
				uint32_t diskUsageMB = CalculateCapturesDiskUsage();
				float diskUsageGB = static_cast<float>(diskUsageMB) / 1024.0f;

				// Use color-coded value display for disk usage
				Util::ColorCodedValueConfig diskUsageConfig = Util::ColorCodedValueConfig::HighIsBad(0.1f, 1.0f, 5.0f);
				diskUsageConfig.tooltipText = "Total size of all capture files in the captures directory";

				Util::DrawColorCodedValue("Capture Size", diskUsageGB, std::format("{:.2f} GB", diskUsageGB), diskUsageConfig);

				if (diskUsageMB > 0) {
					ImGui::SameLine();
					if (ImGui::Button("Clear All Captures")) {
						ImGui::OpenPopup("Confirm Clear Captures##RenderDoc");
					}
				}

				if (ImGui::BeginPopup("Confirm Clear Captures##RenderDoc")) {
					ImGui::Text("Are you sure you want to delete all capture files?");
					ImGui::Text("This will permanently remove %u MB of capture data.", diskUsageMB);
					ImGui::Separator();

					if (ImGui::Button("Yes, Delete All")) {
						ClearFrameCaptures();
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel")) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}
		}

		// Capture Files Section
		{
			auto filesSection = Util::SectionWrapper("Capture Files", "View and manage individual capture files");
			if (filesSection) {
				// Get cached capture files (auto-refreshes every 5 seconds)
				const auto& captureFiles = GetCachedCaptureFiles();

				// Refresh button
				if (ImGui::Button("Refresh List")) {
					RefreshCaptureFileCache();
				}

				ImGui::SameLine();
				ImGui::TextDisabled("(%zu files)", captureFiles.size());

				if (captureFiles.empty()) {
					ImGui::TextDisabled("No capture files found.");
				} else {
					// Display custom table with double-click and hover support
					if (ImGui::BeginTable("##RenderDocCaptures", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
						// Setup headers
						ImGui::TableSetupColumn("Filename", ImGuiTableColumnFlags_DefaultSort);
						ImGui::TableSetupColumn("Size");
						ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
						ImGui::TableHeadersRow();

						// Create a sorted copy of the capture files for display
						static std::vector<CaptureFileInfo> sortedCaptureFiles;
						static std::chrono::steady_clock::time_point sortedCacheLastUpdate = std::chrono::steady_clock::time_point::min();
						static ImGuiTableSortSpecs* sortSpecs = nullptr;

						// Update sorted copy if cache has been refreshed or sorting specs changed
						bool needsSortUpdate = (cacheLastUpdate != sortedCacheLastUpdate) || (sortedCaptureFiles.size() != cachedCaptureFiles.size());

						// Handle sorting
						if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
							sortSpecs = specs;
							if (specs->SpecsDirty || needsSortUpdate) {
								// Copy the current cache and sort it
								sortedCaptureFiles = cachedCaptureFiles;
								std::sort(sortedCaptureFiles.begin(), sortedCaptureFiles.end(),
									[specs](const CaptureFileInfo& a, const CaptureFileInfo& b) {
										for (int i = 0; i < specs->SpecsCount; ++i) {
											const ImGuiTableColumnSortSpecs* spec = &specs->Specs[i];
											int col = spec->ColumnIndex;
											bool ascending = spec->SortDirection == ImGuiSortDirection_Ascending;

											int cmp = 0;
											switch (col) {
											case 0:  // Filename
												cmp = a.filename.compare(b.filename);
												break;
											case 1:  // Size
												cmp = (a.fileSize < b.fileSize) ? -1 : (a.fileSize > b.fileSize) ? 1 :
											                                                                       0;
												break;
											case 2:  // Created (time)
												cmp = (a.lastWriteTime < b.lastWriteTime) ? -1 : (a.lastWriteTime > b.lastWriteTime) ? 1 :
											                                                                                           0;
												break;
											}

											if (cmp != 0) {
												return ascending ? (cmp < 0) : (cmp > 0);
											}
										}
										return false;
									});

								specs->SpecsDirty = false;
								sortedCacheLastUpdate = cacheLastUpdate;
							}
						} else if (needsSortUpdate) {
							// No sorting specs, just copy the cache (newest first by default)
							sortedCaptureFiles = cachedCaptureFiles;
							sortedCacheLastUpdate = cacheLastUpdate;
						}

						// Display rows from the sorted copy
						for (size_t i = 0; i < sortedCaptureFiles.size(); ++i) {
							const auto& file = sortedCaptureFiles[i];
							ImGui::TableNextRow();

							// Filename column with double-click and hover
							ImGui::TableSetColumnIndex(0);
							bool isSelected = false;
							if (ImGui::Selectable(file.filename.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
								if (ImGui::IsMouseDoubleClicked(0)) {
									// Double-clicked - open the file
									try {
										ShellExecuteW(nullptr, L"open", file.fullPath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
										logger::info("[RenderDoc] Opened capture file: {}", file.fullPath.string());
									} catch (const std::exception& e) {
										logger::error("[RenderDoc] Failed to open capture file '{}': {}", file.fullPath.string(), e.what());
									}
								}
							}

							// Hover tooltip for filename
							if (ImGui::IsItemHovered()) {
								std::string tooltip = std::format("File: {}\nSize: {}\nCreated: {}",
									file.fullPath.string(), file.sizeStr, file.timeAgo);

								ImGui::SetTooltip("%s", tooltip.c_str());
							}

							// Size column
							ImGui::TableSetColumnIndex(1);
							ImGui::TextUnformatted(file.sizeStr.c_str());

							// Created column
							ImGui::TableSetColumnIndex(2);
							ImGui::TextUnformatted(file.timeAgo.c_str());
						}

						ImGui::EndTable();
					}

					ImGui::TextDisabled("Double-click a filename to open the capture file");
					ImGui::TextDisabled("Hover over filenames for file details");
				}
			}
		}

		// Intelligent cache refreshing: refresh when section becomes visible
		if (isSectionVisible && !wasSectionVisible) {
			InvalidateCaptureCache();
		}
		wasSectionVisible = isSectionVisible;
	}
}

std::string RenderDoc::GetCapturesDirectory() const
{
	auto path = GetCapturesPath();
	Util::FileHelpers::EnsureDirectoryExists(path);
	return path.string();
}

uint32_t RenderDoc::CalculateCapturesDiskUsage()
{
	try {
		auto path = GetCapturesPath();
		// Accumulate the total size of all capture files in the directory
		uint64_t totalSize = 0;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
			if (ec) {
				logger::warn("[RenderDoc] Failed to iterate captures directory '{}': {}", path.string(), ec.message());
				break;
			}

			if (entry.is_regular_file()) {
				totalSize += entry.file_size(ec);
				if (ec) {
					logger::warn("[RenderDoc] Failed to get file size for '{}': {}", entry.path().string(), ec.message());
				}
			}
		}

		return static_cast<uint32_t>(totalSize / (1024 * 1024));  // Return size in MB
	} catch (const std::exception& e) {
		logger::error("[RenderDoc] Exception in CalculateCapturesDiskUsage: {}", e.what());
		return 0;
	}
}

void RenderDoc::ClearFrameCaptures()
{
	try {
		auto path = GetCapturesPath();
		// Remove all files in the captures directory using safe deletion
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
			if (ec) {
				logger::warn("[RenderDoc] Failed to iterate captures directory '{}': {}", path.string(), ec.message());
				break;
			}

			if (entry.is_regular_file()) {
				// Use safe delete with proper error handling and logging
				auto deleteResult = Util::FileHelpers::SafeDelete(entry.path().string(), "capture file");
				if (!deleteResult.success) {
					logger::warn("[RenderDoc] Failed to delete capture file '{}': {}", entry.path().string(), deleteResult.errorMessage);
				}
			}
		}
	} catch (const std::exception& e) {
		logger::error("[RenderDoc] Exception in ClearFrameCaptures: {}", e.what());
	}
}

std::filesystem::path RenderDoc::GetCapturesPath() const
{
	return Util::PathHelpers::GetCommunityShaderPath() / "Captures";
}

std::filesystem::path RenderDoc::GetRenderDocDllPath() const
{
	// RenderDoc DLL should be in Data/Renderdoc/
	return Util::PathHelpers::GetDataPath() / "Renderdoc" / "renderdoc.dll";
}

void RenderDoc::SetupResources()
{
	// RenderDoc doesn't need any D3D resources
}

void RenderDoc::SaveSettings(json& o_json)
{
	o_json["Enable RenderDoc Capture"] = enableRenderDocCapture;
}

void RenderDoc::LoadSettings(json& o_json)
{
	if (o_json.contains("Enable RenderDoc Capture") && o_json["Enable RenderDoc Capture"].is_boolean()) {
		enableRenderDocCapture = o_json["Enable RenderDoc Capture"];
	}
}

void RenderDoc::RestoreDefaultSettings()
{
	enableRenderDocCapture = false;
}

void RenderDoc::ClearShaderCache()
{
	// RenderDoc doesn't have shaders to clear
}

void RenderDoc::RefreshCaptureFileCache()
{
	cachedCaptureFiles.clear();
	cacheValid = false;

	try {
		auto capturesPath = GetCapturesPath();
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(capturesPath, ec)) {
			if (ec || !entry.is_regular_file())
				continue;

			CaptureFileInfo info;
			info.fullPath = entry.path();
			info.filename = entry.path().filename().string();
			info.fileSize = entry.file_size(ec);
			if (ec)
				continue;

			info.sizeStr = Util::FormatFileSize(info.fileSize);

			info.lastWriteTime = entry.last_write_time(ec);
			if (!ec) {
				info.timeAgo = Util::FormatTimeAgo(info.lastWriteTime);
			} else {
				info.timeAgo = "Unknown";
			}

			cachedCaptureFiles.push_back(info);
		}

		// Sort by modification time (newest first)
		std::sort(cachedCaptureFiles.begin(), cachedCaptureFiles.end(),
			[](const CaptureFileInfo& a, const CaptureFileInfo& b) {
				return a.lastWriteTime > b.lastWriteTime;
			});

		cacheLastUpdate = std::chrono::steady_clock::now();
		cacheValid = true;

		// Apply automatic comments to any new captures
		ApplyAutomaticCommentsToNewCaptures();
	} catch (const std::exception& e) {
		logger::warn("[RenderDoc] Failed to refresh capture file cache: {}", e.what());
		cacheValid = false;
	}
}

const std::vector<CaptureFileInfo>& RenderDoc::GetCachedCaptureFiles()
{
	// Check if cache needs refresh (invalidate after 5 seconds or if not valid)
	auto now = std::chrono::steady_clock::now();
	auto cacheAge = now - cacheLastUpdate;
	auto maxAge = std::chrono::seconds(kCacheRefreshIntervalSeconds);

	if (!cacheValid || cacheAge > maxAge) {
		RefreshCaptureFileCache();
	}

	return cachedCaptureFiles;
}

void RenderDoc::InvalidateCaptureCache()
{
	cacheValid = false;
	cacheLastUpdate = std::chrono::steady_clock::time_point::min();
}

void RenderDoc::TriggerCapture()
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot trigger capture - RenderDoc API not available");
		return;
	}

	logger::info("[RenderDoc] Triggering immediate capture");
	renderDocApi->TriggerCapture();

	// Invalidate cache so it refreshes when next accessed (capture should appear in list)
	InvalidateCaptureCache();
}

void RenderDoc::SetCaptureFilePathTemplate(const std::string& a_template)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot set capture template - RenderDoc API not available");
		return;
	}

	renderDocApi->SetCaptureFilePathTemplate(a_template.c_str());
	logger::info("[RenderDoc] Set capture file path template to: {}", a_template);
}

bool RenderDoc::IsCapturing() const
{
	if (!renderDocApi)
		return false;

	// RenderDoc API doesn't have a direct IsCapturing method, but we can check if captures are enabled
	return enableRenderDocCapture && renderDocApi != nullptr;
}

std::string RenderDoc::GetCapturePath(uint32_t a_index)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot get capture path - RenderDoc API not available");
		return "";
	}

	uint32_t numCaptures = renderDocApi->GetNumCaptures();
	if (a_index >= numCaptures) {
		logger::warn("[RenderDoc] Capture index {} out of range ({} captures available)", a_index, numCaptures);
		return "";
	}

	// Get the required buffer size first
	uint32_t pathLength = 0;
	uint32_t result = renderDocApi->GetCapture(a_index, nullptr, &pathLength, nullptr);
	if (result == 0 || pathLength == 0) {
		logger::warn("[RenderDoc] Failed to get capture path length for index {}", a_index);
		return "";
	}

	// Allocate buffer and get the path
	std::vector<char> pathBuffer(pathLength + 1, 0);
	result = renderDocApi->GetCapture(a_index, pathBuffer.data(), &pathLength, nullptr);
	if (result == 0) {
		logger::warn("[RenderDoc] Failed to get capture path for index {}", a_index);
		return "";
	}

	return std::string(pathBuffer.data());
}

uint32_t RenderDoc::GetNumCaptures() const
{
	if (!renderDocApi)
		return 0;
	return renderDocApi->GetNumCaptures();
}

void RenderDoc::SetPendingCaptureComments(std::string a_comments)
{
	std::lock_guard<std::mutex> lock(pendingCommentsMutex);
	pendingCaptureComments = std::move(a_comments);
	logger::info("[RenderDoc] Set pending capture comments: {}", pendingCaptureComments);
}

void RenderDoc::ApplyPendingCaptureComments(uint32_t a_index)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot apply capture comments - RenderDoc API not available");
		return;
	}

	std::lock_guard<std::mutex> lock(pendingCommentsMutex);
	if (pendingCaptureComments.empty()) {
		return;
	}

	uint32_t numCaptures = renderDocApi->GetNumCaptures();
	if (a_index >= numCaptures) {
		logger::warn("[RenderDoc] Cannot apply comments to capture {} - index out of range ({} captures available)", a_index, numCaptures);
		return;
	}

	// Get the capture file path
	std::string capturePath = GetCapturePath(a_index);
	if (capturePath.empty()) {
		logger::warn("[RenderDoc] Cannot get capture path for index {}", a_index);
		return;
	}

	// Use RenderDoc's built-in SetCaptureFileComments API to embed comments in the .rdc file
	renderDocApi->SetCaptureFileComments(capturePath.c_str(), pendingCaptureComments.c_str());
	logger::info("[RenderDoc] Applied comments to capture {}: {}", a_index, pendingCaptureComments);

	pendingCaptureComments.clear();
}

std::string RenderDoc::BuildAutomaticCaptureComments(const std::string& userComments) const
{
	std::string comments;

	// Runtime information
	auto runtime = REL::Module::GetRuntime();
	auto runtimeName = std::string{ magic_enum::enum_name(runtime) };
	auto gameVersion = Util::GetFormattedVersion(REL::Module::get().version());
	comments += std::format("Skyrim {} {}\n", runtimeName, gameVersion);

	// Plugin version
	auto pluginVersion = Util::GetFormattedVersion(Plugin::VERSION);
	comments += std::format("Community Shaders {}\n", pluginVersion);

	// Enabled features
	const auto& features = Feature::GetFeatureList();
	std::vector<std::string> enabledFeatures;
	for (auto* feature : features) {
		if (feature->loaded) {
			std::string featVersion = feature->version.empty() ? "unknown" : feature->version;
			enabledFeatures.push_back(std::format("{} ({})", feature->GetShortName(), featVersion));
		}
	}

	// Sort features alphabetically for easier comparison
	std::sort(enabledFeatures.begin(), enabledFeatures.end());

	if (!enabledFeatures.empty()) {
		comments += "Enabled Features:\n";
		for (const auto& feature : enabledFeatures) {
			comments += std::format("- {}\n", feature);
		}
	}

	// Add user comments if provided
	if (!userComments.empty()) {
		comments += "\nUser Comments:\n";
		comments += userComments;
	}

	return comments;
}

void RenderDoc::ApplyAutomaticCommentsToNewCaptures()
{
	if (!renderDocApi) {
		return;
	}

	uint32_t numCaptures = renderDocApi->GetNumCaptures();
	if (numCaptures <= lastCaptureCount) {
		return;  // No new captures
	}

	// Check for pending user comments and apply them to the first new capture
	std::string userComments;
	{
		std::lock_guard<std::mutex> lock(pendingCommentsMutex);
		if (!pendingCaptureComments.empty()) {
			userComments = std::move(pendingCaptureComments);
			pendingCaptureComments.clear();
		}
	}

	// Build automatic comments for new captures
	std::string automaticComments = BuildAutomaticCaptureComments("");

	// Apply comments to new captures: user comments to first capture, automatic to others
	for (uint32_t i = lastCaptureCount; i < numCaptures; ++i) {
		std::string capturePath = GetCapturePath(i);
		if (!capturePath.empty()) {
			// Use user comments for the first new capture, automatic for subsequent ones
			const std::string& commentsToApply = (i == lastCaptureCount && !userComments.empty()) ? userComments : automaticComments;
			renderDocApi->SetCaptureFileComments(capturePath.c_str(), commentsToApply.c_str());
			logger::info("[RenderDoc] Applied comments to capture {}: {}", i, commentsToApply);
		}
	}

	lastCaptureCount = numCaptures;
}

std::string RenderDoc::GetOverlayWarningMessage() const
{
	return "WARNING: RenderDoc capture is active, performance will be severely impacted.\n"
		   "Upscaling and Framegeneration may be incompatible.\n"
		   "Press F12, Print Screen or press the Capture button in the RenderDoc feature settings.\n"
		   "Disable RenderDoc capture in the RenderDoc feature settings.";
}
