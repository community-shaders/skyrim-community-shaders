#include "FeatureIssues.h"
#include "Feature.h"

#include "Menu.h"
#include "State.h"
#include "Util.h"

namespace FeatureIssues
{
	// Forward declarations
	static void DrawFeatureIssue(const FeatureIssueInfo& issue, const ImVec4& color);

	// Static storage for feature issues
	static std::vector<FeatureIssueInfo> s_featureIssues;

	// Cache for feature lookup to avoid repeated iterations
	struct FeatureLookupCache
	{
		std::unordered_map<std::string, Feature*> featuresByShortName;
		bool initialized = false;

		void Initialize()
		{
			if (initialized)
				return;

			const auto& features = Feature::GetFeatureList();
			for (auto* feature : features) {
				featuresByShortName[feature->GetShortName()] = feature;
			}
			initialized = true;
		}

		Feature* FindFeature(const std::string& shortName)
		{
			Initialize();
			auto it = featuresByShortName.find(shortName);
			return (it != featuresByShortName.end()) ? it->second : nullptr;
		}
	};

	static FeatureLookupCache s_featureLookupCache;

	// Known obsolete features data
	static const std::map<std::string, FeatureIssueInfo> s_obsoleteFeatureData = {
		{ "ComplexParallaxMaterials", { .shortName = "ComplexParallaxMaterials",
										  .displayName = "Complex Parallax Materials",
										  .rejectionReason = "Integrated into ExtendedMaterials feature",
										  .replacementFeature = "ExtendedMaterials",
										  .userMessage = "This functionality is now built into Community Shaders. Remove the old feature as it's no longer needed.",
										  .removedInVersion = { 1, 0, 0 },
										  .modifiedShaderDirectory = false,
										  .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "TerrainBlending", { .shortName = "TerrainBlending",
								 .displayName = "Terrain Blending",
								 .rejectionReason = "Feature removed due to broken implementation causing visual artifacts",
								 .replacementFeature = "",
								 .userMessage = "This feature has been removed due to visual artifacts. No replacement is available. Remove it from your setup.",
								 .removedInVersion = { 1, 0, 0 },
								 .modifiedShaderDirectory = false,
								 .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "TreeLODLighting", { .shortName = "TreeLODLighting",
								 .displayName = "Tree LOD Lighting",
								 .rejectionReason = "Functionality integrated into base CS lighting system",
								 .replacementFeature = "",
								 .userMessage = "This functionality is now built into Community Shaders. Remove the old feature as it's no longer needed.",
								 .removedInVersion = { 1, 0, 0 },
								 .modifiedShaderDirectory = true,
								 .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "WaterBlending", { .shortName = "WaterBlending",
							   .displayName = "Water Blending",
							   .rejectionReason = "Replaced by unified WaterEffects feature",
							   .replacementFeature = "WaterEffects",
							   .userMessage = "Water blending functionality is now part of WaterEffects. Install WaterEffects instead for comprehensive water improvements.",
							   .removedInVersion = { 1, 0, 0 },
							   .modifiedShaderDirectory = true,
							   .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "WaterCaustics", { .shortName = "WaterCaustics",
							   .displayName = "Water Caustics",
							   .rejectionReason = "Replaced by unified WaterEffects feature",
							   .replacementFeature = "WaterEffects",
							   .userMessage = "Water caustics functionality is now part of WaterEffects. Install WaterEffects instead for comprehensive water improvements.",
							   .removedInVersion = { 1, 0, 0 },
							   .modifiedShaderDirectory = true,
							   .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "WaterParallax", { .shortName = "WaterParallax",
							   .displayName = "Water Parallax",
							   .rejectionReason = "Replaced by unified WaterEffects feature",
							   .replacementFeature = "WaterEffects",
							   .userMessage = "Water parallax functionality is now part of WaterEffects. Install WaterEffects instead for comprehensive water improvements.",
							   .removedInVersion = { 1, 0, 0 },
							   .modifiedShaderDirectory = true,
							   .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "DistantTreeLighting", { .shortName = "DistantTreeLighting",
									 .displayName = "Distant Tree Lighting",
									 .rejectionReason = "Replaced by TreeLODLighting, which was later integrated into CS core",
									 .replacementFeature = "",
									 .userMessage = "This functionality is now built into Community Shaders. Remove the old feature as it's no longer needed.",
									 .removedInVersion = { 0, 8, 0 },
									 .modifiedShaderDirectory = true,
									 .issueType = FeatureIssueInfo::IssueType::OBSOLETE } }
	};

	const std::vector<FeatureIssueInfo>& GetFeatureIssues()
	{
		return s_featureIssues;
	}

	void ClearFeatureIssues()
	{
		s_featureIssues.clear();
	}

	bool HasFeatureIssues()
	{
		return !s_featureIssues.empty();
	}

	bool HasObsoleteShaderModifyingFeatures()
	{
		return std::any_of(s_featureIssues.begin(), s_featureIssues.end(),
			[](const auto& issue) {
				return issue.issueType == FeatureIssueInfo::IssueType::OBSOLETE && issue.ModifiedShaderDirectory();
			});
	}

	bool HasPotentialShaderModifyingFeatures()
	{
		return std::any_of(s_featureIssues.begin(), s_featureIssues.end(),
			[](const auto& issue) {
				return (issue.issueType == FeatureIssueInfo::IssueType::OBSOLETE && issue.ModifiedShaderDirectory()) ||
			           issue.issueType == FeatureIssueInfo::IssueType::UNKNOWN;
			});
	}

	FeatureFileInfo GetFeatureFileInfo(const std::string& featureName)
	{
		FeatureFileInfo info;
		info.featureName = featureName;
		info.latestTimestamp = std::filesystem::file_time_type::min();

		auto updateTimestamp = [&info](const std::filesystem::path& filePath) {
			try {
				auto timestamp = std::filesystem::last_write_time(filePath);
				if (timestamp > info.latestTimestamp) {
					info.latestTimestamp = timestamp;
					info.latestTimestampFile = filePath.string();
				}
			} catch (const std::filesystem::filesystem_error& e) {
				logger::warn("Error getting timestamp for {}: {}", filePath.string(), e.what());
			}
		};

		std::filesystem::path currentPath = std::filesystem::current_path();
		std::filesystem::path dataPath = currentPath / "Data";
		std::filesystem::path deployedIniPath = dataPath / "Shaders" / "Features" / (featureName + ".ini");
		std::filesystem::path deployedShaderDir = dataPath / "Shaders" / featureName;

		// Check for deployed INI file
		if (std::filesystem::exists(deployedIniPath)) {
			info.hasINI = true;
			info.iniPath = deployedIniPath.string();
			updateTimestamp(deployedIniPath);
		}

		// Check for deployed shader directory and HLSL files
		if (std::filesystem::exists(deployedShaderDir)) {
			info.hasDeployedFolder = true;
			info.deployedFolderPath = deployedShaderDir.string();
			updateTimestamp(deployedShaderDir);

			// Scan for HLSL files in deployed location
			try {
				for (const auto& hlslEntry : std::filesystem::recursive_directory_iterator(deployedShaderDir)) {
					if (hlslEntry.is_regular_file()) {
						std::string ext = hlslEntry.path().extension().string();
						if (ext == ".hlsl" || ext == ".hlsli") {
							info.hlslFiles.push_back(hlslEntry.path().string());
							updateTimestamp(hlslEntry.path());
						}
					}
				}
			} catch (const std::filesystem::filesystem_error& e) {
				logger::warn("Error scanning deployed shader directory {}: {}", deployedShaderDir.string(), e.what());
			}
		}

		// Convert timestamp to human-readable format
		if (info.latestTimestamp != std::filesystem::file_time_type::min()) {
			try {
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					info.latestTimestamp - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
				auto time_t = std::chrono::system_clock::to_time_t(sctp);
				std::stringstream ss;
				ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
				info.timestampDisplay = ss.str();
			} catch (const std::exception& e) {
				info.timestampDisplay = "Unknown";
				logger::warn("Error formatting timestamp: {}", e.what());
			}
		} else {
			info.timestampDisplay = "No files found";
		}

		return info;
	}

	void AddFeatureIssue(const std::string& shortName, const std::string& version,
		const std::string& reason, FeatureIssueInfo::IssueType issueType,
		const FeatureFileInfo& fileInfo, const std::string& minimumVersionRequired)
	{
		FeatureIssueInfo issue;
		issue.shortName = shortName;
		issue.version = version;
		issue.rejectionReason = reason;
		issue.issueType = issueType;
		issue.fileInfo = fileInfo;
		issue.minimumVersionRequired = minimumVersionRequired;

		// Check if this "unknown" feature is actually a known obsolete feature
		if (issueType == FeatureIssueInfo::IssueType::UNKNOWN) {
			if (auto it = s_obsoleteFeatureData.find(shortName); it != s_obsoleteFeatureData.end()) {
				// Convert to obsolete type and populate full info
				issue.issueType = FeatureIssueInfo::IssueType::OBSOLETE;
				issue.displayName = it->second.displayName;
				issue.replacementFeature = it->second.replacementFeature;
				issue.userMessage = it->second.userMessage;
				issue.removedInVersion = it->second.removedInVersion;
				issue.rejectionReason = it->second.rejectionReason;
				issue.modifiedShaderDirectory = it->second.modifiedShaderDirectory;

				// Log with obsolete-specific information
				logger::warn("Found obsolete feature INI: {} version {}", shortName, version);
				logger::info("  Reason: {}", issue.rejectionReason);
				if (!issue.replacementFeature.empty()) {
					logger::info("  Replacement: {}", issue.replacementFeature);
				}
				logger::info("  Action: {}", issue.userMessage);
			}
		}

		// For explicitly obsolete features, populate additional info from our data
		if (issueType == FeatureIssueInfo::IssueType::OBSOLETE) {
			if (auto it = s_obsoleteFeatureData.find(shortName); it != s_obsoleteFeatureData.end()) {
				issue.displayName = it->second.displayName;
				issue.replacementFeature = it->second.replacementFeature;
				issue.userMessage = it->second.userMessage;
				issue.removedInVersion = it->second.removedInVersion;
				issue.modifiedShaderDirectory = it->second.modifiedShaderDirectory;
			}
		}

		// Cache replacement feature information for efficient access (only if there's actually a replacement)
		if (!issue.replacementFeature.empty()) {
			Feature* replacementFeatureObj = s_featureLookupCache.FindFeature(issue.replacementFeature);
			if (replacementFeatureObj) {
				issue.replacementFeatureDisplayName = replacementFeatureObj->GetName();
				issue.replacementFeatureInstalled = replacementFeatureObj->loaded;
				issue.replacementFeatureModLink = replacementFeatureObj->IsCore() ? "" : replacementFeatureObj->GetFeatureModLink();
			} else {
				issue.replacementFeatureDisplayName = issue.replacementFeature;  // Fallback to short name
				issue.replacementFeatureInstalled = false;
				issue.replacementFeatureModLink = "";
			}
		} else {
			// For version mismatch features without replacement, cache the current feature's info for download links
			if (issueType == FeatureIssueInfo::IssueType::VERSION_MISMATCH) {
				Feature* featureObj = s_featureLookupCache.FindFeature(shortName);
				if (featureObj) {
					issue.replacementFeatureDisplayName = featureObj->GetName();
					issue.replacementFeatureInstalled = false;  // Not installed (wrong version)
					issue.replacementFeatureModLink = featureObj->IsCore() ? "" : featureObj->GetFeatureModLink();
				} else {
					issue.replacementFeatureDisplayName = shortName;  // Fallback to short name
					issue.replacementFeatureInstalled = false;
					issue.replacementFeatureModLink = "";
				}
			}
			// For unknown features and obsolete without replacement, leave replacement fields empty
		}

		s_featureIssues.push_back(issue);
	}

	bool DeleteFeatureFiles(const FeatureIssueInfo& issue)
	{
		bool allSuccessful = true;
		std::vector<std::string> deletedFiles;
		std::vector<std::string> failedFiles;

		// Helper function to safely delete a path
		auto safeDelete = [&](const std::string& path, const std::string& description) {
			if (path.empty() || !std::filesystem::exists(path)) {
				return;
			}

			try {
				if (std::filesystem::is_directory(path)) {
					std::filesystem::remove_all(path);
				} else {
					std::filesystem::remove(path);
				}
				deletedFiles.push_back(description + ": " + path);
				logger::info("Deleted {}: {}", description, path);
			} catch (const std::filesystem::filesystem_error& e) {
				failedFiles.push_back(description + ": " + path + " (" + e.what() + ")");
				logger::error("Failed to delete {}: {} - {}", description, path, e.what());
				allSuccessful = false;
			}
		};

		// Delete INI file
		if (issue.fileInfo.hasINI) {
			safeDelete(issue.fileInfo.iniPath, "INI file");
		}

		// Delete deployed shader directory
		if (issue.fileInfo.hasDeployedFolder) {
			safeDelete(issue.fileInfo.deployedFolderPath, "Shader directory");
		}

		// Log summary
		if (!deletedFiles.empty()) {
			logger::info("Successfully deleted {} file(s) for feature '{}':", deletedFiles.size(), issue.shortName);
			for (const auto& file : deletedFiles) {
				logger::info("  - {}", file);
			}
		}

		if (!failedFiles.empty()) {
			logger::error("Failed to delete {} file(s) for feature '{}':", failedFiles.size(), issue.shortName);
			for (const auto& file : failedFiles) {
				logger::error("  - {}", file);
			}
		}

		return allSuccessful;
	}

	void DrawFeatureIssuesUI()
	{
		// Get theme colors from Menu system
		const auto menu = Menu::GetSingleton();
		const auto& theme = menu->GetTheme();

		const auto& featureIssues = GetFeatureIssues();

		if (featureIssues.empty()) {
			ImGui::TextWrapped("No feature issues found!");
			ImGui::TextWrapped("All feature INI files are loading successfully.");
			return;
		}

		// Separate issues by type for better organization
		std::vector<const FeatureIssueInfo*> shaderBreakingIssues;
		std::vector<const FeatureIssueInfo*> unknownIssues;
		std::vector<const FeatureIssueInfo*> obsoleteIssues;
		std::vector<const FeatureIssueInfo*> versionIssues;

		for (const auto& issue : featureIssues) {
			if (issue.IsObsolete() && issue.ModifiedShaderDirectory()) {
				// Obsolete shader-modifying features are compilation breaking
				shaderBreakingIssues.push_back(&issue);
			} else if (issue.IsUnknown()) {
				// Unknown features are potentially compilation breaking but separate
				unknownIssues.push_back(&issue);
			} else if (issue.IsObsolete()) {
				obsoleteIssues.push_back(&issue);
			} else if (issue.IsVersionMismatch()) {
				versionIssues.push_back(&issue);
			}
		}

		// Shader Breaking Features Section (most critical)
		if (!shaderBreakingIssues.empty()) {
			ImGui::TextColored(theme.StatusPalette.Error, "Compilation Breaking Features");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following features modified core shader files and must be completely uninstalled via your mod manager. "
				"Deleting just the INI file will not fix compilation errors if core shaders were modified.");
			ImGui::Spacing();

			for (const auto* issue : shaderBreakingIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Error);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Unknown Features Section (potentially compilation breaking)
		if (!unknownIssues.empty()) {
			ImGui::TextColored(theme.StatusPalette.Error, "Unknown Features");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following features are not recognized and were disabled automatically. "
				"They may be from development branches or newer CS versions. Since we cannot determine what files they may have modified, "
				"they should be removed as a precaution to prevent potential shader compilation issues.");
			ImGui::Spacing();

			for (const auto* issue : unknownIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Error);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Obsolete Features Section (non-shader-breaking)
		if (!obsoleteIssues.empty()) {
			ImGui::TextColored(theme.StatusPalette.Warning, "Obsolete Features");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following features are absolute and disabled automatically. "
				"These features have been removed or replaced in this CS version but do not modify core shaders.");
			ImGui::Spacing();

			for (const auto* issue : obsoleteIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Warning);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Version Mismatch Section
		if (!versionIssues.empty()) {
			ImGui::TextColored(theme.StatusPalette.Warning, "Wrong Version Features");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following features have version compatibility issues and were disabled automatically.");
			ImGui::Spacing();

			for (const auto* issue : versionIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Warning);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Common cleanup actions section
		ImGui::TextColored(theme.Palette.Text, "Cleanup Actions:");

		if (ImGui::Button("Open Features Folder")) {
			std::filesystem::path featuresPath = std::filesystem::current_path() / "Data" / "Shaders" / "Features";
			ShellExecuteA(NULL, "open", featuresPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Opens the Features folder containing INI files for manual review.");
		}

		ImGui::SameLine();
		if (ImGui::Button("Open Shaders Directory")) {
			std::filesystem::path shadersPath = std::filesystem::current_path() / "Data" / "Shaders";
			ShellExecuteA(NULL, "open", shadersPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Opens the main Shaders directory to view individual feature shader folders.");
		}

		ImGui::SameLine();
		if (ImGui::Button("Clear Issue List")) {
			ClearFeatureIssues();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Clears this issue list (useful after cleanup).");
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Cleanup guidance
		ImGui::TextColored(theme.Palette.Text, "General Actions:");
		ImGui::BulletText("Use 'Open Features Folder' to manually review INI files");
		ImGui::BulletText("Use 'Open Shaders Directory' to check for orphaned shader folders");
		ImGui::BulletText("Use 'Clear Issue List' to refresh after manual cleanup");
	}

	static void DrawFeatureIssue(const FeatureIssueInfo& issue, const ImVec4& color)
	{
		// Get theme colors directly
		auto menu = Menu::GetSingleton();
		const auto& theme = menu->GetTheme();

		ImGui::PushID(issue.shortName.c_str());

		// Show feature name with appropriate color
		ImGui::Bullet();
		ImGui::SameLine();
		ImGui::TextColored(color, "%s",
			issue.displayName.empty() ? issue.shortName.c_str() : issue.displayName.c_str());

		// Show detailed information in tooltip
		if (auto _tt = Util::HoverTooltipWrapper()) {
			// Show compilation failure warning at the top in red if applicable
			if ((issue.IsObsolete() && issue.ModifiedShaderDirectory()) || issue.IsUnknown()) {
				ImGui::TextColored(color, "POTENTIAL COMPILATION FAILURE");
				if (issue.IsUnknown()) {
					ImGui::TextWrapped("This unknown feature may have modified core shader files and could be causing compilation failures. Unknown features should be removed if failures continue.");
				} else {
					ImGui::TextWrapped("This obsolete feature modified core shader files and is causing compilation failures. It must be uninstalled via mod manager.");
				}
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			if (!issue.iniPath.empty()) {
				ImGui::TextWrapped("INI Path: %s", issue.iniPath.c_str());
				ImGui::Spacing();
			}
			if (!issue.version.empty()) {
				ImGui::TextWrapped("Current Version: %s", issue.version.c_str());
				ImGui::Spacing();
			}
			if (issue.IsVersionMismatch() && !issue.minimumVersionRequired.empty()) {
				ImGui::TextWrapped("Minimum Required: %s", issue.minimumVersionRequired.c_str());
				ImGui::Spacing();
			}
			ImGui::TextWrapped("Issue: %s", issue.rejectionReason.c_str());

			if (issue.IsObsolete() && !issue.replacementFeature.empty()) {
				ImGui::Spacing();
				ImGui::TextWrapped("Replacement: %s", issue.replacementFeatureDisplayName.c_str());
			}

			if (issue.IsObsolete() && !issue.userMessage.empty()) {
				ImGui::Spacing();
				ImGui::TextWrapped("Guidance: %s", issue.userMessage.c_str());
			}

			// Show file information
			if (issue.fileInfo.hasINI || issue.fileInfo.hasDeployedFolder) {
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::TextColored(theme.Palette.Text, "Files:");

				if (issue.fileInfo.hasINI) {
					ImGui::TextWrapped("INI: %s", issue.fileInfo.iniPath.c_str());
				}
				if (issue.fileInfo.hasDeployedFolder) {
					ImGui::TextWrapped("Shader Folder: %s", issue.fileInfo.deployedFolderPath.c_str());
					if (!issue.fileInfo.hlslFiles.empty()) {
						ImGui::TextWrapped("HLSL Files: %zu found", issue.fileInfo.hlslFiles.size());
					}
				}

				// Show timestamp information
				if (!issue.fileInfo.timestampDisplay.empty()) {
					ImGui::Spacing();
					ImGui::TextColored(theme.Palette.Text, "Last Modified:");
					ImGui::TextWrapped("Time: %s", issue.fileInfo.timestampDisplay.c_str());
					if (!issue.fileInfo.latestTimestampFile.empty()) {
						ImGui::TextWrapped("File: %s", issue.fileInfo.latestTimestampFile.c_str());
					}
				}
			}
		}

		// Handle replacement feature actions for obsolete features
		if (issue.IsObsolete() && !issue.replacementFeature.empty()) {
			// Show replacement info using friendly name with emphasis
			ImGui::SameLine();
			ImGui::Text("(replaced by ");
			ImGui::SameLine(0, 0);  // No spacing
			ImGui::TextColored(theme.StatusPalette.RestartNeeded, "%s", issue.replacementFeatureDisplayName.c_str());
			ImGui::SameLine(0, 0);  // No spacing
			ImGui::Text(")");

			if (issue.replacementFeatureInstalled) {
				// Show "Open" button to navigate to the replacement feature
				ImGui::SameLine();

				if (ImGui::SmallButton(("Open " + issue.replacementFeatureDisplayName + " Settings").c_str())) {
					// Navigate to the replacement feature in the menu
					menu->SelectFeatureMenu(issue.replacementFeature);
					logger::debug("User requested to open {} feature menu", issue.replacementFeature);
				}

				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Open the installed %s feature settings", issue.replacementFeatureDisplayName.c_str());
				}
			} else {
				// Check if replacement feature has a download link (cached)
				if (!issue.replacementFeatureModLink.empty()) {
					ImGui::SameLine();

					if (ImGui::SmallButton(("Download " + issue.replacementFeatureDisplayName).c_str())) {
						ShellExecuteA(0, 0, issue.replacementFeatureModLink.c_str(), 0, 0, SW_SHOW);
					}

					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Download the replacement feature: %s", issue.replacementFeatureDisplayName.c_str());
					}
				}
			}
		}

		// Handle download action for version mismatch features
		if (issue.IsVersionMismatch()) {
			ImGui::SameLine();

			if (!issue.replacementFeatureModLink.empty()) {
				std::string buttonText = issue.minimumVersionRequired.empty() ?
				                             ("Download Latest " + issue.replacementFeatureDisplayName) :
				                             ("Download " + issue.replacementFeatureDisplayName + " " + issue.minimumVersionRequired + "+");

				if (ImGui::SmallButton(buttonText.c_str())) {
					ShellExecuteA(0, 0, issue.replacementFeatureModLink.c_str(), 0, 0, SW_SHOW);
				}

				if (auto _tt = Util::HoverTooltipWrapper()) {
					if (!issue.minimumVersionRequired.empty()) {
						ImGui::Text("Download %s version %s or later", issue.replacementFeatureDisplayName.c_str(), issue.minimumVersionRequired.c_str());
					} else {
						ImGui::Text("Download the latest version of %s", issue.replacementFeatureDisplayName.c_str());
					}
				}
			} else {
				// Show message when no download link is available
				std::string updateText = issue.minimumVersionRequired.empty() ?
				                             "Update Required" :
				                             ("Update to " + issue.minimumVersionRequired + "+ Required");

				ImGui::TextWrapped("%s", updateText.c_str());
				if (auto _tt = Util::HoverTooltipWrapper()) {
					if (!issue.minimumVersionRequired.empty()) {
						ImGui::Text("This feature needs to be updated to version %s or later. Check the mod page manually.", issue.minimumVersionRequired.c_str());
					} else {
						ImGui::Text("This feature needs to be updated but no download link is available. Check the mod page manually.");
					}
				}
			}
		}

		// Show download button for any feature with a download link (even if no replacement)
		if (!issue.IsVersionMismatch() && !issue.IsObsolete() && !issue.replacementFeatureModLink.empty()) {
			ImGui::SameLine();

			if (ImGui::SmallButton(("Download " + issue.replacementFeatureDisplayName).c_str())) {
				ShellExecuteA(0, 0, issue.replacementFeatureModLink.c_str(), 0, 0, SW_SHOW);
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Download %s", issue.replacementFeatureDisplayName.c_str());
			}
		}

		// Only show delete button for features that don't modify shader directories (and we know they don't)
		if (!issue.ModifiedShaderDirectory()) {
			ImGui::SameLine();
			std::string deleteButtonId = "Delete##" + issue.shortName;
			std::string confirmPopupId = "Confirm Delete##" + issue.shortName;

			if (ImGui::SmallButton(deleteButtonId.c_str())) {
				ImGui::OpenPopup(confirmPopupId.c_str());
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (issue.IsUnknown()) {
					ImGui::Text("Delete files for this unknown feature. WARNING: If this feature modified core shaders, deletion may not fix compilation issues.");
				} else {
					ImGui::Text("Delete all files associated with this feature (INI, shaders, etc.)");
				}
			}

			// Confirmation popup for deletion
			if (ImGui::BeginPopupModal(confirmPopupId.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextWrapped("Are you sure? This will delete all files for feature '%s'?",
					issue.displayName.empty() ? issue.shortName.c_str() : issue.displayName.c_str());
				ImGui::Spacing();

				// Enhanced warning for unknown features
				if (issue.IsUnknown()) {
					ImGui::TextColored(theme.StatusPalette.Error, "WARNING:");
					ImGui::TextWrapped("This is an UNKNOWN feature. If it modified core shader files (outside of its own folder), deleting these files alone will NOT fix shader compilation issues.");
					ImGui::Spacing();
					ImGui::TextColored(theme.StatusPalette.Warning, "If compilation issues persist after deletion:");
					ImGui::BulletText("Completely uninstall the feature via your mod manager");
					ImGui::BulletText("Check for modified files in Data/Shaders/ (not in feature subfolders)");
					ImGui::BulletText("Consider reinstalling Community Shaders if issues persist");
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();
				}

				ImGui::TextColored(theme.StatusPalette.Warning, "This will delete:");
				if (issue.fileInfo.hasINI) {
					ImGui::BulletText("INI file: %s", issue.fileInfo.iniPath.c_str());
				}
				if (issue.fileInfo.hasDeployedFolder) {
					ImGui::BulletText("Shader directory: %s", issue.fileInfo.deployedFolderPath.c_str());
					if (!issue.fileInfo.hlslFiles.empty()) {
						ImGui::BulletText("%zu HLSL files", issue.fileInfo.hlslFiles.size());
					}
				}

				ImGui::Spacing();
				ImGui::TextColored(theme.StatusPalette.Error, "This action cannot be undone!");
				ImGui::Spacing();

				if (ImGui::Button("Delete", ImVec2(120, 0))) {
					if (DeleteFeatureFiles(issue)) {
						// Remove from issues list after successful deletion
						auto& issues = const_cast<std::vector<FeatureIssueInfo>&>(GetFeatureIssues());
						issues.erase(std::remove_if(issues.begin(), issues.end(),
										 [&issue](const FeatureIssueInfo& i) { return i.shortName == issue.shortName; }),
							issues.end());
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::PopID();
	}

	bool IsReplacementFeatureInstalled(const std::string& featureName)
	{
		Feature* feature = s_featureLookupCache.FindFeature(featureName);
		return feature ? feature->loaded : false;
	}

	std::string FeatureIssues::GetFeatureModLink(const std::string& featureName)
	{
		Feature* feature = s_featureLookupCache.FindFeature(featureName);
		if (feature && !feature->IsCore()) {
			return feature->GetFeatureModLink();
		}
		return "";
	}

	bool IsObsoleteFeature(const std::string& featureName)
	{
		// Check if the feature is in our obsolete features map
		return s_obsoleteFeatureData.find(featureName) != s_obsoleteFeatureData.end();
	}

	void ScanForOrphanedFeatureINIs()
	{
		std::filesystem::path currentPath = std::filesystem::current_path();
		std::filesystem::path featuresPath = currentPath / "Data" / "Shaders" / "Features";

		if (!std::filesystem::exists(featuresPath)) {
			return;
		}

		// Get list of active feature names
		std::set<std::string> activeFeatureNames;
		const auto& features = Feature::GetFeatureList();
		for (auto* feature : features) {
			activeFeatureNames.insert(feature->GetShortName());
		}

		// Scan for INI files
		try {
			for (const auto& entry : std::filesystem::directory_iterator(featuresPath)) {
				if (entry.is_regular_file() && entry.path().extension() == ".ini") {
					std::string featureName = entry.path().stem().string();

					// Skip if this feature is in the active list (it will be processed normally)
					if (activeFeatureNames.find(featureName) != activeFeatureNames.end()) {
						continue;
					}

					// Skip VR feature when not in VR mode (it's a core feature)
					if (featureName == "VR" && !REL::Module::IsVR()) {
						logger::info("Ignoring VR.ini in non-VR mode");
						continue;
					}

					// This is an orphaned INI file - check if it's a known obsolete feature
					if (IsObsoleteFeature(featureName)) {
						// Read version from INI file
						CSimpleIniA ini;
						ini.SetUnicode();
						ini.LoadFile(entry.path().c_str());

						std::string version = "unknown";
						if (auto value = ini.GetValue("Info", "Version")) {
							version = value;
						}

						FeatureFileInfo fileInfo = GetFeatureFileInfo(featureName);
						AddFeatureIssue(featureName, version,
							std::format("{} is an obsolete feature that has been removed", featureName),
							FeatureIssueInfo::IssueType::OBSOLETE, fileInfo);

						logger::warn("Found orphaned obsolete feature INI: {} version {}", featureName, version);
					} else {
						// Unknown orphaned feature
						FeatureFileInfo fileInfo = GetFeatureFileInfo(featureName);
						AddFeatureIssue(featureName, "unknown",
							std::format("{} is not recognized by this CS version", featureName),
							FeatureIssueInfo::IssueType::UNKNOWN, fileInfo);

						logger::warn("Found orphaned unknown feature INI: {}", featureName);
					}
				}
			}
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error scanning Features directory: {}", e.what());
		}
	}
}
