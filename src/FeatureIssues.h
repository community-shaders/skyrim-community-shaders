#pragma once

#include "Globals.h"
#include <filesystem>
#include <string>
#include <vector>

/**
 * Centralized system for tracking and managing feature issues
 * (obsolete features, rejected INI files, version mismatches, etc.)
 */
namespace FeatureIssues
{
	/**
	 * Information about feature files and directories
	 */
	struct FeatureFileInfo
	{
		std::string featureName;             // Short name of the feature
		std::string deployedFolderPath;      // Path to deployed shader folder (Data/Shaders/FeatureName/)
		std::string iniPath;                 // Path to the INI file (Data/Shaders/Features/FeatureName.ini)
		std::vector<std::string> hlslFiles;  // List of HLSL files for this feature
		bool hasDeployedFolder;              // Whether the deployed shader folder exists
		bool hasINI;                         // Whether INI file exists in deployed location

		// Timestamp information for file tracking
		std::filesystem::file_time_type latestTimestamp;  // Latest modification time across all files
		std::string latestTimestampFile;                  // Path to the file with the latest timestamp
		std::string timestampDisplay;                     // Human-readable timestamp string
	};

	/**
	 * Comprehensive information about a feature that has issues
	 */
	struct FeatureIssueInfo
	{
		std::string shortName;           // Short name of the feature
		std::string displayName;         // Display name of the feature (empty if unknown)
		std::string version;             // Version found in INI (if any)
		std::string iniPath;             // Full path to the INI file
		std::string rejectionReason;     // Why it was rejected/obsolete
		std::string replacementFeature;  // What feature replaced it (if any)
		std::string userMessage;         // Guidance message for user
		REL::Version removedInVersion;   // CS version when it was removed (for obsolete features)
		bool modifiedShaderDirectory;    // Whether this obsolete feature modified package/Shaders/ directly
		FeatureFileInfo fileInfo;        // Detailed file information

		enum class IssueType
		{
			OBSOLETE,          // Known obsolete feature with replacement info
			VERSION_MISMATCH,  // Feature exists but version is incompatible
			UNKNOWN            // Feature not recognized by this CS version
		};

		IssueType issueType;

		// Helper methods
		bool IsObsolete() const { return issueType == IssueType::OBSOLETE; }
		bool IsVersionMismatch() const { return issueType == IssueType::VERSION_MISMATCH; }
		bool IsUnknown() const { return issueType == IssueType::UNKNOWN; }
		bool HasReplacement() const { return !replacementFeature.empty(); }
		bool ModifiedShaderDirectory() const { return modifiedShaderDirectory; }
	};

	/**
	 * Get list of features with issues (obsolete, rejected, unknown, etc.)
	 *
	 * \return Reference to vector of feature issue information
	 */
	const std::vector<FeatureIssueInfo>& GetFeatureIssues();

	/**
	 * Clear the list of feature issues (useful after cleanup operations)
	 */
	void ClearFeatureIssues();

	/**
	 * Check if there are any feature issues to display
	 * @return true if there are any feature issues that need attention
	 */
	bool HasFeatureIssues();

	/**
	 * Check if any obsolete features that modified shader directory are present
	 * This helps identify potential shader compilation issues
	 * @return true if any obsolete shader-modifying features are detected
	 */
	bool HasObsoleteShaderModifyingFeatures();

	/**
	 * Get detailed file information for a feature
	 * This helps users understand the actual file structure
	 *
	 * \param featureName Short name of the feature to analyze
	 * \return Feature file information
	 */
	FeatureFileInfo GetFeatureFileInfo(const std::string& featureName);

	/**
	 * Add a feature issue to the tracking system
	 *
	 * \param shortName Short name of the feature
	 * \param version Version found in INI (if any)
	 * \param reason Why it was rejected/obsolete
	 * \param issueType Type of issue
	 * \param fileInfo Detailed file information
	 */
	void AddFeatureIssue(const std::string& shortName, const std::string& version,
		const std::string& reason, FeatureIssueInfo::IssueType issueType,
		const FeatureFileInfo& fileInfo = {});

	/**
	 * Draw UI for feature issues (rejected and obsolete features)
	 */
	void DrawFeatureIssuesUI();

	/**
	 * Delete feature directory and related files safely
	 *
	 * \param issue The feature issue containing file information
	 * \return true if deletion was successful, false otherwise
	 */
	bool DeleteFeatureFiles(const FeatureIssueInfo& issue);

	/**
	 * Check if a feature is obsolete
	 *
	 * \param featureName Short name of the feature
	 * \return true if the feature is obsolete, false otherwise
	 */
	static bool IsObsoleteFeature(const std::string& featureName)
	{
		// Check if the feature is in our obsolete features map
		return s_obsoleteFeatureData.find(featureName) != s_obsoleteFeatureData.end();
	}

	/**
	 * Get the mod download link for a replacement feature (if available and not core)
	 *
	 * \param featureName Short name of the feature to look up
	 * \return Download link if available, empty string if feature is core or has no link
	 */
	std::string GetFeatureModLink(const std::string& featureName);
}
