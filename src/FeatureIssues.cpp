#include "FeatureIssues.h"
#include "Feature.h"

#include "Menu.h"
#include "State.h"
#include "Util.h"
#include "Utils/Format.h"

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
		std::filesystem::path deployedIniPath = Util::PathHelpers::GetFeatureIniPath(featureName);
		std::filesystem::path deployedShaderDir = Util::PathHelpers::GetFeatureShaderPath(featureName);

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

		// Check for duplicates before adding
		auto existingIssue = std::find_if(s_featureIssues.begin(), s_featureIssues.end(),
			[&shortName](const FeatureIssueInfo& existing) {
				return existing.shortName == shortName;
			});

		if (existingIssue != s_featureIssues.end()) {
			// Update existing issue with new information if this one has more details
			if (issueType == FeatureIssueInfo::IssueType::OBSOLETE &&
				existingIssue->issueType == FeatureIssueInfo::IssueType::UNKNOWN) {
				// Upgrade unknown to obsolete with full details
				*existingIssue = issue;
				logger::debug("Updated existing unknown issue to obsolete for feature: {}", shortName);
			} else {
				logger::debug("Skipping duplicate feature issue for: {}", shortName);
			}
			return;
		}

		s_featureIssues.push_back(issue);
	}
	bool DeleteFeatureFiles(const FeatureIssueInfo& issue)
	{
		bool allSuccessful = true;
		std::vector<std::string> deletedFiles;
		std::vector<std::string> failedFiles;

		// Delete INI file
		if (issue.fileInfo.hasINI) {
			auto result = Util::FileHelpers::SafeDelete(issue.fileInfo.iniPath, "INI file");
			if (result.success) {
				deletedFiles.push_back(result.deletedDescription);
			} else {
				failedFiles.push_back(result.deletedDescription + " (" + result.errorMessage + ")");
				allSuccessful = false;
			}
		}

		// Delete deployed shader directory
		if (issue.fileInfo.hasDeployedFolder) {
			auto result = Util::FileHelpers::SafeDelete(issue.fileInfo.deployedFolderPath, "Shader directory");
			if (result.success) {
				deletedFiles.push_back(result.deletedDescription);
			} else {
				failedFiles.push_back(result.deletedDescription + " (" + result.errorMessage + ")");
				allSuccessful = false;
			}
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
		if (auto section = Util::SectionWrapper("Compilation Breaking Features",
				"The following features modified core shader files and must be completely uninstalled via your mod manager. "
				"Deleting just the INI file will not fix compilation errors if core shaders were modified.",
				theme.StatusPalette.Error, !shaderBreakingIssues.empty())) {
			for (const auto* issue : shaderBreakingIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Error);
			}
		}
		// Unknown Features Section (potentially compilation breaking)
		if (auto section = Util::SectionWrapper("Unknown Features",
				"The following features are not recognized and we tried to disable automatically. "
				"They may be from development branches or newer CS versions. Since we cannot determine what files they may have modified, "
				"they should be removed as a precaution to prevent potential shader compilation failures.",
				theme.StatusPalette.Error, !unknownIssues.empty())) {
			for (const auto* issue : unknownIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Error);
			}
		}
		// Obsolete Features Section (non-shader-breaking)
		if (auto section = Util::SectionWrapper("Obsolete Features",
				"The following features are obsolete and disabled automatically. "
				"These features have been removed or replaced in this CS version but do not modify core shaders.",
				theme.StatusPalette.Warning, !obsoleteIssues.empty())) {
			for (const auto* issue : obsoleteIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Warning);
			}
		}
		// Version Mismatch Section
		if (auto section = Util::SectionWrapper("Wrong Version Features",
				"The following features have version compatibility issues and were disabled automatically. Updating them may resolve the issues.",
				theme.StatusPalette.Warning, !versionIssues.empty())) {
			for (const auto* issue : versionIssues) {
				DrawFeatureIssue(*issue, theme.StatusPalette.Warning);
			}
		}

		// Common cleanup actions section
		ImGui::TextColored(theme.Palette.Text, "Cleanup Actions:");
		if (ImGui::Button("Open Features Folder")) {
			std::filesystem::path featuresPath = Util::PathHelpers::GetFeaturesPath();
			ShellExecuteA(NULL, "open", featuresPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Opens the Features folder containing INI files for manual review.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Open Shaders Directory")) {
			std::filesystem::path shadersPath = Util::PathHelpers::GetShadersPath();
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
				ImGui::TextWrapped("Current Version: %s", Util::CleanVersionString(issue.version).c_str());
				ImGui::Spacing();
			}
			if (issue.IsVersionMismatch() && !issue.minimumVersionRequired.empty()) {
				ImGui::TextWrapped("Minimum Required: %s", Util::CleanVersionString(issue.minimumVersionRequired).c_str());
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
				                             ("Download " + issue.replacementFeatureDisplayName + " " + Util::CleanVersionString(issue.minimumVersionRequired) + "+");

				if (ImGui::SmallButton(buttonText.c_str())) {
					ShellExecuteA(0, 0, issue.replacementFeatureModLink.c_str(), 0, 0, SW_SHOW);
				}

				if (auto _tt = Util::HoverTooltipWrapper()) {					if (!issue.minimumVersionRequired.empty()) {
						ImGui::Text("Download %s version %s or later", issue.replacementFeatureDisplayName.c_str(), Util::CleanVersionString(issue.minimumVersionRequired).c_str());
					} else {
						ImGui::Text("Download the latest version of %s", issue.replacementFeatureDisplayName.c_str());
					}
				}
			} else {
				// Show message when no download link is available
				std::string updateText = issue.minimumVersionRequired.empty() ?
				                             "Update Required" :
				                             ("Update to " + Util::CleanVersionString(issue.minimumVersionRequired) + "+ Required");

				ImGui::TextWrapped("%s", updateText.c_str());
				if (auto _tt = Util::HoverTooltipWrapper()) {
	if (!issue.minimumVersionRequired.empty()) {
						ImGui::Text("This feature needs to be updated to version %s or later. Check the mod page manually.", Util::CleanVersionString(issue.minimumVersionRequired).c_str());
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
		// Show delete button for:
		// 1. Features that don't modify shader directories (safe to delete)
		// 2. Obsolete features with replacements (user can install replacement after deletion)
		bool canSafelyDelete = !issue.ModifiedShaderDirectory() || (issue.IsObsolete() && !issue.replacementFeature.empty());
		if (canSafelyDelete) {
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

	void ScanForOrphanedFeatureINIs(bool checkLoadedFeatures)
	{
		std::filesystem::path featuresPath = Util::PathHelpers::GetFeaturesPath();

		if (!std::filesystem::exists(featuresPath)) {
			return;
		}

		// Get list of active feature names
		std::set<std::string> activeFeatureNames;
		const auto& features = Feature::GetFeatureList();
		for (auto* feature : features) {
			activeFeatureNames.insert(feature->GetShortName());
		}

		// If requested, check loaded features for issues (e.g., features that failed to load)
		if (checkLoadedFeatures) {
			for (auto* feature : features) {
				// Re-add issues for features that were not successfully loaded
				if (!feature->loaded && !feature->failedLoadedMessage.empty()) {
					FeatureFileInfo fileInfo = GetFeatureFileInfo(feature->GetShortName());
					// For features that failed to load, we'll assume version mismatch as the most common cause
					// The original error message and details were already constructed during initial loading
					AddFeatureIssue(feature->GetShortName(), feature->version, feature->failedLoadedMessage,
						FeatureIssueInfo::IssueType::VERSION_MISMATCH, fileInfo);
				}
			}
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
	// Developer mode test INI functionality
	namespace Test
	{
		// Static storage for tracking test INIs across session
		static std::vector<TestIniInfo> s_activeTestInis;

		// Path for persistent test state file
		static std::filesystem::path GetTestStateFilePath()
		{
			return Util::PathHelpers::GetFeaturesPath() / "CSDevTestState.test";
		}

		// TestIniInfo method implementations
		bool TestIniInfo::stillExists() const
		{
			return std::filesystem::exists(testIniPath);
		}

		bool TestIniInfo::wasManuallyDeleted() const
		{
			return !std::filesystem::exists(testIniPath);
		}

		bool LoadPersistentTestState()
		{
			const auto stateFilePath = GetTestStateFilePath();
			if (!std::filesystem::exists(stateFilePath)) {
				return false;
			}

			try {
				std::ifstream file(stateFilePath);
				if (!file) {
					return false;
				}

				nlohmann::json stateData;
				file >> stateData;

				s_activeTestInis.clear();
				if (stateData.contains("testInis") && stateData["testInis"].is_array()) {
					for (const auto& testData : stateData["testInis"]) {
						TestIniInfo testInfo;
						testInfo.testIniPath = testData["testIniPath"].get<std::string>();
						testInfo.isNewFile = testData["isNewFile"].get<bool>();
						testInfo.testType = testData["testType"].get<std::string>();
						testInfo.featureName = testData["featureName"].get<std::string>();
						testInfo.originalVersion = testData["originalVersion"].get<std::string>();

						s_activeTestInis.push_back(testInfo);
					}
				}

				logger::debug("Loaded {} test INI records from persistent state", s_activeTestInis.size());
				return !s_activeTestInis.empty();
			} catch (const std::exception& e) {
				logger::warn("Failed to load persistent test state: {}", e.what());
				return false;
			}
		}
		bool SavePersistentTestState()
		{
			const auto stateFilePath = GetTestStateFilePath();

			try {
				nlohmann::json stateData;
				nlohmann::json testArray = nlohmann::json::array();

				for (const auto& testInfo : s_activeTestInis) {
					nlohmann::json testData;
					testData["testIniPath"] = testInfo.testIniPath;
					testData["isNewFile"] = testInfo.isNewFile;
					testData["testType"] = testInfo.testType;
					testData["featureName"] = testInfo.featureName;
					testData["originalVersion"] = testInfo.originalVersion;
					testArray.push_back(testData);
				}

				stateData["testInis"] = testArray;
				stateData["created"] = std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::system_clock::now().time_since_epoch())
				                           .count();

				std::ofstream file(stateFilePath);
				if (!file) {
					return false;
				}

				file << stateData.dump(2);
				logger::debug("Saved {} test INI records to persistent state", s_activeTestInis.size());
				return true;
			} catch (const std::exception& e) {
				logger::warn("Failed to save persistent test state: {}", e.what());
				return false;
			}
		}

		std::string GetTestStateDescription()
		{
			// Always refresh from disk to get the most current state
			LoadPersistentTestState();

			if (s_activeTestInis.empty()) {
				return "No test INI files are currently active.";
			}

			std::string description = std::format("Active test INI files ({}):\n", s_activeTestInis.size());

			int activeCount = 0, deletedCount = 0, obsoleteCount = 0, unknownCount = 0, versionCount = 0;
			std::vector<std::string> obsoleteFeatures, unknownFeatures, versionFeatures, deletedFeatures;

			for (const auto& testInfo : s_activeTestInis) {
				bool exists = testInfo.stillExists();
				bool deleted = testInfo.wasManuallyDeleted();

				if (exists) {
					activeCount++;
				} else if (deleted) {
					deletedCount++;
					deletedFeatures.push_back(testInfo.featureName);
				}

				if (testInfo.testType.find("obsolete") != std::string::npos) {
					obsoleteCount++;
					obsoleteFeatures.push_back(testInfo.featureName + (deleted ? " (deleted)" : ""));
				} else if (testInfo.testType.find("unknown") != std::string::npos) {
					unknownCount++;
					unknownFeatures.push_back(testInfo.featureName + (deleted ? " (deleted)" : ""));
				} else if (testInfo.testType.find("version") != std::string::npos) {
					versionCount++;
					std::string versionInfo = testInfo.featureName;
					if (!testInfo.originalVersion.empty()) {
						versionInfo += " (was: " + testInfo.originalVersion + ")";
					}
					if (deleted) {
						versionInfo += " (deleted)";
					} else if (!testInfo.isNewFile) {
						versionInfo += " (modified)";
					}
					versionFeatures.push_back(versionInfo);
				}
			}

			// Helper function to join vector elements with commas
			auto joinWithCommas = [](const std::vector<std::string>& vec) -> std::string {
				std::string result;
				for (size_t i = 0; i < vec.size(); ++i) {
					if (i > 0)
						result += ", ";
					result += vec[i];
				}
				return result;
			};

			// Detailed breakdown by type
			if (obsoleteCount > 0) {
				description += std::format("Obsolete features ({}): {}\n", obsoleteCount, joinWithCommas(obsoleteFeatures));
			}

			if (unknownCount > 0) {
				description += std::format("Unknown features ({}): {}\n", unknownCount, joinWithCommas(unknownFeatures));
			}

			if (versionCount > 0) {
				description += std::format("Version mismatch ({}): {}\n", versionCount, joinWithCommas(versionFeatures));
			}

			if (deletedCount > 0) {
				description += std::format("\n {} test file(s) manually deleted - markers remain for cleanup", deletedCount);
			}

			if (activeCount < s_activeTestInis.size()) {
				description += "\nSome test files modified - restore recommended to clean up";
			}

			return description;
		}

		std::vector<TestIniInfo>& GetCurrentTestInis()
		{
			// Load persistent state if we haven't already
			if (s_activeTestInis.empty()) {
				LoadPersistentTestState();
			}
			return s_activeTestInis;
		}

		bool HasActiveTestInis()
		{
			// Load persistent state if we haven't already
			if (s_activeTestInis.empty()) {
				LoadPersistentTestState();
			}
			return !s_activeTestInis.empty();
		}

		std::vector<TestIniInfo> CreateTestInis()
		{
			std::vector<TestIniInfo> createdInis;
			const std::filesystem::path featuresPath = Util::PathHelpers::GetFeaturesPath();

			// Ensure Features directory exists
			if (!std::filesystem::create_directories(featuresPath) && !std::filesystem::exists(featuresPath)) {
				logger::error("Failed to create Features directory: {}", featuresPath.string());
				return createdInis;
			}

			logger::info("Creating comprehensive test INI files for feature issue testing...");

			// Get list of loaded features for analysis
			const auto& loadedFeatures = Feature::GetFeatureList();
			std::unordered_map<std::string, Feature*> loadedFeatureMap;
			for (auto* feature : loadedFeatures) {
				loadedFeatureMap[feature->GetShortName()] = feature;
			}
			// 1. Dynamically select optimal obsolete features to cover all test cases
			// We need to test all combinations: shader-breaking/non-shader-breaking × with/without replacement
			struct ObsoleteTestCase
			{
				std::string category;
				bool shaderBreaking;
				bool hasReplacement;
				std::string selectedFeature;
			};

			std::vector<ObsoleteTestCase> requiredTestCases = {
				{ "non-shader-breaking with replacement", false, true, "" },
				{ "non-shader-breaking without replacement", false, false, "" },
				{ "shader-breaking with replacement", true, true, "" },
				{ "shader-breaking without replacement", true, false, "" }
			};

			// Find the best obsolete feature for each test case
			for (auto& testCase : requiredTestCases) {
				for (const auto& [featureName, featureData] : s_obsoleteFeatureData) {
					bool matches = (featureData.modifiedShaderDirectory == testCase.shaderBreaking) &&
					               (!featureData.replacementFeature.empty() == testCase.hasReplacement);

					if (matches && testCase.selectedFeature.empty()) {
						testCase.selectedFeature = featureName;
						logger::debug("Selected {} for test case: {}", featureName, testCase.category);
						break;
					}
				}

				if (testCase.selectedFeature.empty()) {
					logger::warn("Could not find obsolete feature for test case: {}", testCase.category);
				}
			}
			// Create test INIs for selected features
			for (const auto& testCase : requiredTestCases) {
				if (testCase.selectedFeature.empty())
					continue;

				const std::filesystem::path iniPath = featuresPath / (testCase.selectedFeature + ".ini");

				// Skip if already exists to avoid overwriting real files
				if (std::filesystem::exists(iniPath)) {
					logger::warn("Skipping {} test INI creation - file already exists", testCase.selectedFeature);
					continue;
				}

				try {
					// Create test INI content
					std::string iniContent = std::format(
						"[Info]\n"
						"Version = 1-0-0\n"
						"\n"
						"[Settings]\n"
						"# Test INI created by CS Developer Mode for {}\n"
						"# This feature is obsolete and will trigger feature issue detection\n"
						"TestFeature = true\n",
						testCase.category);

					// Write the test INI file
					std::ofstream outFile(iniPath, std::ios::out | std::ios::trunc);
					if (!outFile) {
						throw std::runtime_error("Failed to open file for writing");
					}

					outFile << iniContent;
					outFile.close();

					if (outFile.fail()) {
						throw std::runtime_error("Failed to write file contents");
					}
					TestIniInfo testInfo;
					testInfo.testIniPath = iniPath.string();
					testInfo.isNewFile = true;
					testInfo.testType = "obsolete";
					testInfo.featureName = testCase.selectedFeature;
					createdInis.push_back(testInfo);

					logger::debug("Created {} test INI: {}", testCase.category, iniPath.string());
				} catch (const std::exception& e) {
					logger::warn("Failed to create test INI for {}: {}", testCase.selectedFeature, e.what());
				}
			}

			// 2. Create unknown feature test INI
			const std::string unknownFeature = "CSDevTestUnknownFeature";
			const std::filesystem::path unknownIniPath = featuresPath / (unknownFeature + ".ini");
			if (!std::filesystem::exists(unknownIniPath)) {
				try {
					std::string iniContent =
						"[Info]\n"
						"Version = 9-9-9\n"
						"\n"
						"[Settings]\n"
						"# Unknown test feature created by CS Developer Mode\n"
						"# This will trigger unknown feature issue detection\n"
						"UnknownSetting = true\n";

					std::ofstream outFile(unknownIniPath, std::ios::out | std::ios::trunc);
					if (!outFile) {
						throw std::runtime_error("Failed to open file for writing");
					}

					outFile << iniContent;
					outFile.close();

					if (outFile.fail()) {
						throw std::runtime_error("Failed to write file contents");
					}
					TestIniInfo testInfo;
					testInfo.testIniPath = unknownIniPath.string();
					testInfo.isNewFile = true;
					testInfo.testType = "unknown";
					testInfo.featureName = unknownFeature;
					createdInis.push_back(testInfo);

					logger::debug("Created unknown feature test INI: {}", unknownIniPath.string());
				} catch (const std::exception& e) {
					logger::warn("Failed to create unknown test INI for {}: {}", unknownFeature, e.what());
				}
			} else {
				logger::warn("Skipping {} test INI creation - file already exists", unknownFeature);
			}  // 3. Create version mismatch tests - prioritize unloaded features to avoid disruption
			struct VersionMismatchCandidate
			{
				std::string featureName;
				bool hasModLink;
				bool isLoaded;
				bool hasExistingINI;
				std::string reason;
				int priority;  // Lower = higher priority (safer)
			};

			std::vector<VersionMismatchCandidate> versionCandidates;

			// Analyze ALL features (loaded and unloaded) to find safe version mismatch candidates
			for (const auto& [featureName, feature] : loadedFeatureMap) {
				const std::filesystem::path iniPath = featuresPath / (featureName + ".ini");

				std::string modLink = GetFeatureModLink(featureName);
				bool hasModLink = !modLink.empty();
				bool isLoaded = feature->loaded;
				bool hasExistingINI = std::filesystem::exists(iniPath);

				VersionMismatchCandidate candidate;
				candidate.featureName = featureName;
				candidate.hasModLink = hasModLink;
				candidate.isLoaded = isLoaded;
				candidate.hasExistingINI = hasExistingINI;

				// Priority system: lower number = higher priority (safer)
				if (!isLoaded && !hasExistingINI && hasModLink) {
					candidate.priority = 1;
					candidate.reason = "unloaded feature with mod link (safest - create new INI)";
				} else if (!isLoaded && !hasExistingINI && !hasModLink) {
					candidate.priority = 2;
					candidate.reason = "unloaded feature without mod link (safe - create new INI)";
				} else if (!isLoaded && hasExistingINI && hasModLink) {
					candidate.priority = 3;
					candidate.reason = "unloaded feature with mod link (modify existing INI)";
				} else if (!isLoaded && hasExistingINI && !hasModLink) {
					candidate.priority = 4;
					candidate.reason = "unloaded feature without mod link (modify existing INI)";
				} else if (isLoaded && hasExistingINI && hasModLink) {
					candidate.priority = 5;
					candidate.reason = "loaded feature with mod link (user can redownload if needed)";
				} else if (isLoaded && hasExistingINI && !hasModLink) {
					candidate.priority = 6;
					candidate.reason = "loaded feature without mod link (risky - disrupts user setup)";
				} else {
					// Skip invalid combinations (loaded without INI, etc.)
					continue;
				}

				versionCandidates.push_back(candidate);
			}

			// Sort candidates: by priority (safer first), then by mod link availability, then by name
			std::sort(versionCandidates.begin(), versionCandidates.end(),
				[](const auto& a, const auto& b) {
					if (a.priority != b.priority)
						return a.priority < b.priority;
					if (a.hasModLink != b.hasModLink)
						return a.hasModLink > b.hasModLink;
					return a.featureName < b.featureName;
				});
			// Select best candidates for comprehensive testing
			std::string withModLinkFeature, withoutModLinkFeature;
			for (const auto& candidate : versionCandidates) {
				if (candidate.hasModLink && withModLinkFeature.empty()) {
					withModLinkFeature = candidate.featureName;
				} else if (!candidate.hasModLink && withoutModLinkFeature.empty()) {
					withoutModLinkFeature = candidate.featureName;
				}

				// Stop when we have both types (with and without mod links)
				if (!withModLinkFeature.empty() && !withoutModLinkFeature.empty()) {
					break;
				}
			}

			// Create version mismatch tests for comprehensive coverage
			std::vector<std::string> testFeatures;
			if (!withModLinkFeature.empty())
				testFeatures.push_back(withModLinkFeature);
			if (!withoutModLinkFeature.empty() && withoutModLinkFeature != withModLinkFeature)
				testFeatures.push_back(withoutModLinkFeature);

			// Fallback to first available candidate if we don't have both types
			if (testFeatures.empty() && !versionCandidates.empty()) {
				testFeatures.push_back(versionCandidates[0].featureName);
			}
			bool versionMismatchCreated = false;
			for (const auto& testFeatureName : testFeatures) {
				const std::filesystem::path iniPath = featuresPath / (testFeatureName + ".ini");

				// Find the candidate info for decision making
				auto candidateIt = std::find_if(versionCandidates.begin(), versionCandidates.end(),
					[&testFeatureName](const auto& c) { return c.featureName == testFeatureName; });

				if (candidateIt == versionCandidates.end()) {
					logger::warn("Could not find candidate info for {}, skipping", testFeatureName);
					continue;
				}

				const auto& candidate = *candidateIt;

				try {
					if (candidate.hasExistingINI) {
						// Modify existing INI file (safer for loaded features)
						CSimpleIniA ini;
						ini.SetUnicode();
						SI_Error result = ini.LoadFile(iniPath.c_str());
						if (result < 0) {
							throw std::runtime_error("Failed to load existing INI file");
						}

						// Get the original version (if any)
						std::string originalVersion = "none";
						if (auto value = ini.GetValue("Info", "Version")) {
							originalVersion = value;
						}

						// Set the incompatible version to trigger version mismatch
						ini.SetValue("Info", "Version", "0-0-1");

						// Save the modified INI file
						result = ini.SaveFile(iniPath.c_str());
						if (result < 0) {
							throw std::runtime_error("Failed to save modified INI file");
						}

						TestIniInfo testInfo;
						testInfo.testIniPath = iniPath.string();
						testInfo.isNewFile = false;
						testInfo.testType = "version mismatch";
						testInfo.featureName = testFeatureName;
						testInfo.originalVersion = originalVersion;
						createdInis.push_back(testInfo);

						logger::debug("Modified existing INI for version mismatch test: {} ({})", iniPath.string(), candidate.reason);
					} else {
						// Create new INI file with incompatible version (safest for unloaded features)
						std::string iniContent =
							"[Info]\n"
							"Version = 0-0-1\n"
							"\n"
							"[Settings]\n"
							"# Test INI created by CS Developer Mode for version mismatch testing\n"
							"# This version (0-0-1) is incompatible and will trigger version mismatch detection\n"
							"TestFeature = true\n";

						std::ofstream outFile(iniPath, std::ios::out | std::ios::trunc);
						if (!outFile) {
							throw std::runtime_error("Failed to open file for writing");
						}

						outFile << iniContent;
						outFile.close();

						if (outFile.fail()) {
							throw std::runtime_error("Failed to write file contents");
						}
						TestIniInfo testInfo;
						testInfo.testIniPath = iniPath.string();
						testInfo.isNewFile = true;
						testInfo.testType = "version mismatch";
						testInfo.featureName = testFeatureName;
						testInfo.originalVersion = "none";  // No original version for new files
						createdInis.push_back(testInfo);

						logger::debug("Created new INI for version mismatch test: {} ({})", iniPath.string(), candidate.reason);
					}

					versionMismatchCreated = true;
				} catch (const std::exception& e) {
					logger::warn("Failed to create version mismatch test for {}: {}", testFeatureName, e.what());
				}
			}

			if (!versionMismatchCreated) {
				logger::warn("Could not create version mismatch test - no suitable existing features found");
			}
			// Store the created test INIs for later cleanup and save to persistent state
			s_activeTestInis = createdInis;
			SavePersistentTestState();
			// Immediately scan for feature issues to detect the newly created test INIs
			// This allows the UI to show updated status without requiring a restart
			if (!createdInis.empty()) {
				ScanForOrphanedFeatureINIs();
			}
			// Log summary of what was created
			if (createdInis.empty()) {
				logger::warn("No test INI files were created - check for existing files or permission issues");
			} else {
				logger::info("Created {} test INI files covering all major feature issue test cases. Feature issues will be detected immediately.", createdInis.size());

				int obsoleteCount = 0, unknownCount = 0, versionMismatchCount = 0;
				for (const auto& testInfo : createdInis) {
					std::filesystem::path path(testInfo.testIniPath);
					std::string filename = path.filename().string();
					if (filename.find("ComplexParallax") != std::string::npos ||
						filename.find("WaterBlending") != std::string::npos ||
						filename.find("TerrainBlending") != std::string::npos) {
						obsoleteCount++;
					} else if (filename.find("CSDevTestUnknown") != std::string::npos) {
						unknownCount++;
					} else if (!testInfo.isNewFile) {
						versionMismatchCount++;
					}
				}

				logger::info("Test coverage: {} obsolete, {} unknown, {} version mismatch",
					obsoleteCount, unknownCount, versionMismatchCount);
			}

			return createdInis;
		}

		bool RestoreOriginalState(const std::vector<TestIniInfo>& testInis)
		{
			bool success = true;
			logger::info("Restoring original state by cleaning up test INI files...");

			for (const auto& testInfo : testInis) {
				try {
					if (testInfo.isNewFile) {
						// Remove the test INI file we created
						if (std::filesystem::exists(testInfo.testIniPath)) {
							std::filesystem::remove(testInfo.testIniPath);
							logger::debug("Removed test INI: {}", testInfo.testIniPath);
						}
					} else {
						// Restore original version using INI functions
						CSimpleIniA ini;
						ini.SetUnicode();
						SI_Error result = ini.LoadFile(testInfo.testIniPath.c_str());
						if (result >= 0) {
							// Restore the original version
							if (testInfo.originalVersion == "none") {
								// Remove the version key if it wasn't there originally
								ini.Delete("Info", "Version");
							} else {
								// Restore the original version
								ini.SetValue("Info", "Version", testInfo.originalVersion.c_str());
							}

							// Save the restored INI file
							result = ini.SaveFile(testInfo.testIniPath.c_str());
							if (result >= 0) {
								logger::debug("Restored original version in INI: {}", testInfo.testIniPath);
							} else {
								throw std::runtime_error("Failed to save restored INI file");
							}
						} else {
							throw std::runtime_error("Failed to load INI file for restoration");
						}
					}
				} catch (const std::exception& e) {
					logger::warn("Failed to restore INI {}: {}", testInfo.testIniPath, e.what());
					success = false;
				}
			}  // Clear the active test INIs tracking and remove persistent state
			s_activeTestInis.clear();
			try {
				const auto stateFilePath = GetTestStateFilePath();
				if (std::filesystem::exists(stateFilePath)) {
					std::filesystem::remove(stateFilePath);
				}
			} catch (const std::exception& e) {
				logger::warn("Failed to remove persistent test state file: {}", e.what());
			}
			// Clear existing feature issues and rescan to update UI immediately
			// This ensures the restored state is reflected without requiring a restart
			// Use checkLoadedFeatures=true to detect all issues including from loaded features
			ClearFeatureIssues();
			ScanForOrphanedFeatureINIs(true);

			if (success) {
				logger::info("Successfully restored original state. Feature issues updated.");
			} else {
				logger::warn("Some test INI cleanup operations failed.");
			}
			return success;
		}
		void DrawDeveloperModeTestingUI()
		{
			// Refresh test state from disk and update feature issues to ensure current status
			RefreshTestState();

			// Get theme settings from Menu
			auto* menu = Menu::GetSingleton();
			const auto& themeSettings = menu->GetTheme();

			if (ImGui::CollapsingHeader("Testing", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
				{
					auto sectionWrapper = Util::SectionWrapper("Feature Issue Testing",
						"These tools create test INI files to trigger all known feature issue types for testing purposes.",
						themeSettings.Palette.Text);

					if (sectionWrapper) {
						const bool hasActiveTests = HasActiveTestInis();
						if (hasActiveTests) {  // Warning section using theme colors
							ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.RestartNeeded);
							ImGui::TextWrapped("Test INI files are currently active. Restart CS to see feature issues.");
							ImGui::PopStyleColor();  // Show detailed test state information
							ImGui::Spacing();
							ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.RestartNeeded);
							ImGui::TextWrapped(GetTestStateDescription().c_str());
							ImGui::PopStyleColor();
							ImGui::Spacing();
						}

						// Create Test INIs button
						{
							auto disableGuard = Util::DisableGuard(hasActiveTests);
							auto buttonStyle = Util::StyledButtonWrapper(
								themeSettings.Palette.Border,
								themeSettings.StatusPalette.RestartNeeded,
								themeSettings.StatusPalette.CurrentHotkey);

							if (ImGui::Button("Create Test Inis", { -1, 0 })) {
								auto testInis = CreateTestInis();
								logger::info("Created {} test INI files for feature issue testing", testInis.size());
							}
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text(
								"Creates test INI files that trigger all known feature issue cases:\n"
								"- Obsolete features (ComplexParallaxMaterials, TerrainBlending, etc.)\n"
								"- Unknown features (fake non-existent features)\n"
								"- Version mismatch (modifies existing feature version)\n"
								"Restart CS after creating to see the issues in action.");
						}

						// Restore button
						{
							auto disableGuard = Util::DisableGuard(!hasActiveTests);
							auto buttonStyle = Util::StyledButtonWrapper(
								themeSettings.Palette.Border,
								themeSettings.StatusPalette.Error,
								themeSettings.StatusPalette.CurrentHotkey);

							if (ImGui::Button("Restore", { -1, 0 })) {
								auto& testInis = GetCurrentTestInis();
								if (RestoreOriginalState(testInis)) {
									logger::info("Successfully restored original state");
								} else {
									logger::warn("Some restoration operations failed");
								}
							}
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text(
								"Removes all test INI files and restores any modified INI files to their original state.\n"
								"This undoes all changes made by 'Create Test Inis'.\n"
								"Restart CS after restoring to see normal operation.");
						}
					}
				}
			}
		}
		bool RefreshTestState()
		{
			// Load the latest test state from disk without triggering feature issue scan
			// The scan should only be triggered when actual changes occur (create/restore)
			bool stateLoaded = LoadPersistentTestState();

			if (stateLoaded) {
				logger::debug("Refreshed test state from disk");
			}

			return stateLoaded;
		}

	}  // namespace Test

}
