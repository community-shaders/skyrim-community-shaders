#include "FeatureIssues.h"
#include "Feature.h"

#include "Menu.h"
#include "State.h"
#include "Util.h"

namespace FeatureIssues
{
	// Forward declarations
	static void DrawFeatureIssue(const FeatureIssueInfo& issue, const ImVec4& color, const ImVec4& successColor, const ImVec4& infoColor);

	// Static storage for feature issues
	static std::vector<FeatureIssueInfo> s_featureIssues;
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
								 .userMessage = "This feature has been removed due to visual artifacts. No replacement is available.",
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
							   .userMessage = "Water blending functionality is now part of WaterEffects. Install WaterEffects for comprehensive water improvements.",
							   .removedInVersion = { 1, 0, 0 },
							   .modifiedShaderDirectory = true,
							   .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "WaterCaustics", { .shortName = "WaterCaustics",
							   .displayName = "Water Caustics",
							   .rejectionReason = "Replaced by unified WaterEffects feature",
							   .replacementFeature = "WaterEffects",
							   .userMessage = "Water caustics functionality is now part of WaterEffects. Install WaterEffects for comprehensive water improvements.",
							   .removedInVersion = { 1, 0, 0 },
							   .modifiedShaderDirectory = true,
							   .issueType = FeatureIssueInfo::IssueType::OBSOLETE } },
		{ "WaterParallax", { .shortName = "WaterParallax",
							   .displayName = "Water Parallax",
							   .rejectionReason = "Replaced by unified WaterEffects feature",
							   .replacementFeature = "WaterEffects",
							   .userMessage = "Water parallax functionality is now part of WaterEffects. Install WaterEffects for comprehensive water improvements.",
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
		const FeatureFileInfo& fileInfo)
	{
		FeatureIssueInfo issue;
		issue.shortName = shortName;
		issue.version = version;
		issue.rejectionReason = reason;
		issue.issueType = issueType;
		issue.fileInfo = fileInfo;

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
			}
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
		// Get theme colors from Menu system instead of hard-coded values
		auto menu = Menu::GetSingleton();
		const auto& theme = menu->GetTheme();

		const ImVec4& errorColor = theme.StatusPalette.Error;
		const ImVec4& warningColor = theme.StatusPalette.RestartNeeded;  // Reuse green for warnings/obsolete
		const ImVec4& successColor = theme.StatusPalette.RestartNeeded;
		const ImVec4& infoColor = theme.Palette.Text;

		const auto& featureIssues = GetFeatureIssues();

		if (featureIssues.empty()) {
			ImGui::TextColored(successColor, "No feature issues found!");
			ImGui::TextWrapped("All feature INI files are loading successfully.");
			return;
		}

		// Check if there are shader-modifying obsolete features and show prominent warning
		bool hasShaderModifyingObsolete = HasObsoleteShaderModifyingFeatures();
		if (hasShaderModifyingObsolete) {
			ImGui::PushStyleColor(ImGuiCol_Border, errorColor);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
			ImGui::BeginChild("ShaderWarning", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysAutoResize);

			ImGui::TextColored(errorColor, "⚠️ SHADER COMPILATION IMPACT");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"Some obsolete features below modified shader files directly. "
				"If you experience shader compilation errors, these obsolete features may be the cause. "
				"Remove the obsolete feature INI files and check for leftover shader files.");

			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Separate issues by type for better organization
		std::vector<const FeatureIssueInfo*> obsoleteIssues;
		std::vector<const FeatureIssueInfo*> versionIssues;
		std::vector<const FeatureIssueInfo*> unknownIssues;

		for (const auto& issue : featureIssues) {
			if (issue.IsObsolete()) {
				obsoleteIssues.push_back(&issue);
			} else if (issue.IsVersionMismatch()) {
				versionIssues.push_back(&issue);
			} else {
				unknownIssues.push_back(&issue);
			}
		}

		// Obsolete Features Section
		if (!obsoleteIssues.empty()) {
			ImGui::TextColored(warningColor, "Obsolete Feature INI Files");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following obsolete feature INI files were found. "
				"These features have been removed or replaced in this CS version.");
			ImGui::Spacing();

			for (const auto* issue : obsoleteIssues) {
				DrawFeatureIssue(*issue, warningColor, successColor, infoColor);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Version Mismatch Section
		if (!versionIssues.empty()) {
			ImGui::TextColored(errorColor, "Version Mismatch Issues");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following features have version compatibility issues.");
			ImGui::Spacing();

			for (const auto* issue : versionIssues) {
				DrawFeatureIssue(*issue, errorColor, successColor, infoColor);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Unknown Features Section
		if (!unknownIssues.empty()) {
			ImGui::TextColored(errorColor, "Unknown Feature INI Files");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The following feature INI files are not recognized by this CS version. "
				"They may be from development branches or newer CS versions.");
			ImGui::Spacing();

			for (const auto* issue : unknownIssues) {
				DrawFeatureIssue(*issue, errorColor, successColor, infoColor);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Common cleanup actions section
		ImGui::TextColored(infoColor, "Cleanup Actions:");

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
		ImGui::TextColored(infoColor, "Cleanup Guidance:");
		ImGui::TextWrapped("• Features Folder: Contains INI files - you can safely delete obsolete feature INI files");
		ImGui::TextWrapped("• Shaders Directory: Contains HLSL directories - look for directories that don't match current features");
		ImGui::TextWrapped("• Individual Delete: Each feature above has a 'Delete' button for convenient removal");
		ImGui::TextWrapped("• Timestamps: Each feature shows its latest modification time for reference");
		ImGui::TextWrapped("• Always backup your settings before deleting files");
		ImGui::TextWrapped("• Obsolete features are marked in yellow/orange above");
		ImGui::TextWrapped("• After cleanup, use 'Clear Issue List' to refresh the menu");
	}

	static void DrawFeatureIssue(const FeatureIssueInfo& issue, const ImVec4& color, const ImVec4& successColor, const ImVec4& infoColor)
	{
		ImGui::PushID(issue.shortName.c_str());

		// Show feature name with appropriate color
		ImGui::TextColored(color, "• %s",
			issue.displayName.empty() ? issue.shortName.c_str() : issue.displayName.c_str());

		// If this is an obsolete feature that modified shader directory, show warning indicator
		if (issue.IsObsolete() && issue.ModifiedShaderDirectory()) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "[Shader Impact]");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("This obsolete feature directly modified shader files and may cause compilation issues.");
			}
		}

		// Show detailed information in tooltip
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (!issue.iniPath.empty()) {
				ImGui::TextWrapped("INI Path: %s", issue.iniPath.c_str());
				ImGui::Spacing();
			}
			if (!issue.version.empty()) {
				ImGui::TextWrapped("Version: %s", issue.version.c_str());
				ImGui::Spacing();
			}
			ImGui::TextWrapped("Issue: %s", issue.rejectionReason.c_str());

			if (issue.IsObsolete()) {
				// In DrawFeatureIssue function, after showing replacement text
				if (!issue.replacementFeature.empty()) {
					ImGui::Spacing();
					ImGui::TextWrapped("Replacement: %s", issue.replacementFeature.c_str());

					// Check if replacement feature has a download link (and isn't core)
					std::string replacementModLink = GetFeatureModLink(issue.replacementFeature);
					if (!replacementModLink.empty()) {
						ImGui::SameLine();
						if (ImGui::SmallButton(("Download " + issue.replacementFeature).c_str())) {
							ShellExecuteA(0, 0, replacementModLink.c_str(), 0, 0, SW_SHOW);
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("Click to download the replacement feature");
						}
					}
				}

				if (!issue.userMessage.empty()) {
					ImGui::Spacing();
					ImGui::TextWrapped("Guidance: %s", issue.userMessage.c_str());
				}
			}

			// Show file information
			if (issue.fileInfo.hasINI || issue.fileInfo.hasDeployedFolder) {
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::TextColored(infoColor, "Files:");

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
					ImGui::TextColored(infoColor, "Last Modified:");
					ImGui::TextWrapped("Time: %s", issue.fileInfo.timestampDisplay.c_str());
					if (!issue.fileInfo.latestTimestampFile.empty()) {
						ImGui::TextWrapped("File: %s", issue.fileInfo.latestTimestampFile.c_str());
					}
				}
			}
		}

		// Show inline action buttons
		ImGui::SameLine();
		if (ImGui::SmallButton("Show in Explorer")) {
			std::string folderPath;
			if (!issue.iniPath.empty()) {
				folderPath = std::filesystem::path(issue.iniPath).parent_path().string();
			} else if (issue.fileInfo.hasDeployedFolder) {
				folderPath = issue.fileInfo.deployedFolderPath;
			}
			if (!folderPath.empty()) {
				ShellExecuteA(NULL, "open", folderPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Open the folder containing this feature's files in Windows Explorer");
		}

		// Add delete button for feature files
		ImGui::SameLine();
		std::string deleteButtonId = "Delete##" + issue.shortName;
		std::string confirmPopupId = "Confirm Delete##" + issue.shortName;
		// Use theme error color for delete button to indicate danger
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 1.0f));

		if (ImGui::SmallButton(deleteButtonId.c_str())) {
			ImGui::OpenPopup(confirmPopupId.c_str());
		}

		ImGui::PopStyleColor(3);

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Delete all files associated with this feature (INI, shaders, etc.)");
		}

		// Confirmation popup for deletion
		if (ImGui::BeginPopupModal(confirmPopupId.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("Are you sure you want to delete all files for feature '%s'?",
				issue.displayName.empty() ? issue.shortName.c_str() : issue.displayName.c_str());
			ImGui::Spacing();

			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "This will delete:");
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
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "This action cannot be undone!");
			ImGui::Spacing();

			if (ImGui::Button("Delete All Files", ImVec2(120, 0))) {
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

		// Show replacement info for obsolete features
		if (issue.IsObsolete() && !issue.replacementFeature.empty()) {
			ImGui::SameLine();
			ImGui::TextColored(successColor, "→ %s", issue.replacementFeature.c_str());
		}

		ImGui::PopID();
	}

	std::string FeatureIssues::GetFeatureModLink(const std::string& featureName)
	{
		// Get the feature list and find the matching feature
		const auto& features = Feature::GetFeatureList();
		for (auto* feature : features) {
			if (feature->GetShortName() == featureName) {
				// Only return mod link if it's not a core feature
				if (!feature->IsCore()) {
					return feature->GetFeatureModLink();
				}
			}
		}
		return "";  // No link found or feature is core
	}

	bool IsObsoleteFeature(const std::string& featureName)
	{
		// Check if the feature is in our obsolete features map
		return s_obsoleteFeatureData.find(featureName) != s_obsoleteFeatureData.end();
	}
}
