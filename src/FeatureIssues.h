#pragma once

#include "Globals.h"
#include <filesystem>

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
		bool hasDeployedFolder{ false };     // Whether the deployed shader folder exists
		bool hasINI{ false };                // Whether INI file exists in deployed location

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
		std::string shortName;                  // Short name of the feature
		std::string displayName;                // Display name of the feature (empty if unknown)
		std::string version;                    // Version found in INI (if any)
		std::string iniPath;                    // Full path to the INI file
		std::string rejectionReason;            // Why it was rejected/obsolete
		std::string replacementFeature;         // What feature replaced it (short name)
		std::string userMessage;                // Guidance message for user
		REL::Version removedInVersion;          // CS version when it was removed (for obsolete features)
		bool modifiedShaderDirectory{ false };  // Whether this obsolete feature modified package/Shaders/ directly
		FeatureFileInfo fileInfo;               // Detailed file information

		// Version mismatch specific information
		std::string minimumVersionRequired;  // For version mismatch issues, the minimum version required

		// Cached replacement feature information (populated when issue is added)
		std::string replacementFeatureDisplayName;  // Friendly name of replacement feature
		bool replacementFeatureInstalled{ false };  // Whether replacement is installed and loaded
		std::string replacementFeatureModLink;      // Download link for replacement (if available)

		enum class IssueType
		{
			OBSOLETE,          // Known obsolete feature with replacement info
			VERSION_MISMATCH,  // Feature exists but version is incompatible
			OVERRIDE_FAILED,   // Override file failed to load or apply
			UNKNOWN            // Feature not recognized by this CS version
		};

		IssueType issueType{ IssueType::UNKNOWN };

		// Helper methods
		/** @brief Checks if issue type is OBSOLETE. */
		bool IsObsolete() const { return issueType == IssueType::OBSOLETE; }
		/** @brief Checks if issue type is VERSION_MISMATCH. */
		bool IsVersionMismatch() const { return issueType == IssueType::VERSION_MISMATCH; }
		/** @brief Checks if issue type is OVERRIDE_FAILED. */
		bool IsOverrideFailed() const { return issueType == IssueType::OVERRIDE_FAILED; }
		/** @brief Checks if issue type is UNKNOWN. */
		bool IsUnknown() const { return issueType == IssueType::UNKNOWN; }
		/** @brief Checks whether a replacement feature is specified. */
		bool HasReplacement() const { return !replacementFeature.empty(); }
		/** @brief Checks whether this feature modified the core shader directory. */
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
	 * Check if any features that may have modified core shaders are present
	 * This includes obsolete shader-modifying features and unknown features
	 * @return true if any potentially shader-modifying features are detected
	 */
	bool HasPotentialShaderModifyingFeatures();

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
	 * \param minimumVersionRequired For version mismatch issues, the minimum version required
	 */
	void AddFeatureIssue(const std::string& shortName, const std::string& version,
		const std::string& reason, FeatureIssueInfo::IssueType issueType,
		const FeatureFileInfo& fileInfo = {}, const std::string& minimumVersionRequired = "");

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
	bool IsObsoleteFeature(const std::string& featureName);

	/**
	 * Get the mod download link for a replacement feature (if available and not core)
	 *
	 * \param featureName Short name of the feature to look up
	 * \return Download link if available, empty string if feature is core or has no link
	 */
	std::string GetFeatureModLink(const std::string& featureName);

	/**
	 * Check if a replacement feature is installed and loaded
	 *
	 * \param featureName Short name of the feature to check
	 * \return true if the feature is installed and loaded, false otherwise
	 */
	bool IsReplacementFeatureInstalled(const std::string& featureName);
	/**
	 * Scan for orphaned feature INI files that are not in the active feature list
	 *
	 * This function scans the Data/Shaders/Features/ directory for INI files that
	 * correspond to features not currently in the active feature list (e.g., obsolete
	 * features, unknown features). It identifies whether
	 * these orphaned INI files are known obsolete features or completely unknown features
	 * and adds them to the feature issues tracking system.
	 *
	 * Should be called after all active features have been loaded to detect leftover
	 * INI files that might cause issues or confusion.
	 *
	 * @param checkLoadedFeatures If true, also checks loaded features for issues like version mismatches.
	 *                           Defaults to false to maintain backward compatibility for startup scans.
	 *                           Should be set to true for refresh operations to ensure all errors are detected.
	 */
	void ScanForOrphanedFeatureINIs(bool checkLoadedFeatures = false);

	/**
	 * Developer mode functionality for testing feature issues.
	 * These functions are only available when IsDeveloperMode() returns true.
	 */
	namespace Test
	{
		/**
		 * Structure to track test INI files and any backups made
		 */
		struct TestIniInfo
		{
			std::string testIniPath;      // Path to the test INI file created
			bool isNewFile{ true };       // Whether this is a completely new file or modified existing
			std::string testType;         // Description of test type (obsolete, unknown, version mismatch)
			std::string featureName;      // Name of the feature being tested
			std::string originalVersion;  // Original version string (for version mismatch tests)

			// Status tracking for cross-restart functionality
			bool stillExists() const;         // Check if test INI still exists
			bool wasManuallyDeleted() const;  // Check if user manually deleted the test INI
		};

		/**
		 * Creates test INI files that trigger all known feature issue types.
		 * This includes:
		 * - Obsolete features (ComplexParallaxMaterials, TerrainBlending, etc.)
		 * - Unknown features (fake non-existent features)
		 * - Version mismatch (modify existing feature with incompatible version)
		 *
		 * @return Vector of created test INI information for cleanup
		 */
		std::vector<TestIniInfo> CreateTestInis();

		/**
		 * Restores the original state by removing test INI files and restoring backups.
		 *
		 * @param testInis Vector of test INI information from CreateTestInis()
		 * @return true if all cleanup operations were successful
		 */
		bool RestoreOriginalState(const std::vector<TestIniInfo>& testInis);

		/**
		 * Get current test INI information (persistent across calls)
		 * @return Reference to current test INI tracking
		 */
		std::vector<TestIniInfo>& GetCurrentTestInis();

		/**
		 * Check if test INIs are currently active
		 * @return true if test INIs have been created and not yet restored
		 */
		bool HasActiveTestInis();

		/**
		 * Load persistent test INI tracking from disk (survives restarts)
		 * @return true if any active test data was loaded
		 */
		bool LoadPersistentTestState();

		/**
		 * Save persistent test INI tracking to disk
		 * @return true if successfully saved
		 */
		bool SavePersistentTestState();

		/**
		 * @brief Get detailed status of all test INIs for tooltip display.
		 * @return String describing current test state and any issues
		 */
		std::string GetTestStateDescription();

		/**
		 * Refresh test state from disk without triggering feature issue scan
		 * This should be called when the UI is drawn to ensure current state
		 * @return true if test state was successfully loaded/refreshed
		 */
		bool RefreshTestState();

		/**
		 * Draw the developer mode testing UI section
		 * This includes test INI creation/restore functionality with proper theming
		 */
		void DrawDeveloperModeTestingUI();
	}

}
