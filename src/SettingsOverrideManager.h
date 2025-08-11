#pragma once

#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

/**
 * @brief Manages layered JSON override system for Community Shaders features
 *
 * This class handles discovery and application of feature setting overrides
 * from external mod files without requiring changes to existing feature code.
 *
 * Override files follow the format: {ModName}_{FeatureShortName}.json
 * or {ModName}_Global.json for global overrides affecting multiple features.
 */
class SettingsOverrideManager
{
public:
	struct OverrideInfo
	{
		std::string modName;
		std::string featureName;  // Empty for global overrides
		std::string filePath;
		json overrideData;
		bool isGlobal = false;

		// Metadata from override file
		std::string version;
		std::string description;
		bool enabled = true;

		// Tracking for first-time application
		std::string fileHash;          // Hash of override file content for change detection
		std::time_t firstApplied = 0;  // Timestamp when first applied
	};

	static SettingsOverrideManager* GetSingleton()
	{
		static SettingsOverrideManager instance;
		return &instance;
	}

	/**
	 * @brief Discovers all override files in the overrides directory
	 * @return Number of override files discovered
	 */
	size_t DiscoverOverrides();

	/**
	 * @brief Applies overrides to settings JSON, respecting applied override tracking
	 * @param baseSettings The default settings to start with
	 * @param appliedOverrides Reference to tracking data for applied overrides
	 * @return Modified settings with overrides applied (only new/changed overrides)
	 */
	size_t ApplyNewOverrides(json& baseSettings, json& appliedOverrides);

	/**
	 * @brief Applies overrides to a specific feature's settings JSON
	 * @param featureName The short name of the feature
	 * @param featureJson The feature's JSON settings to modify
	 * @return Number of overrides applied
	 */
	size_t ApplyOverrides(const std::string& featureName, json& featureJson);

	/**
	 * @brief Applies feature-specific overrides respecting applied override tracking
	 * @param featureName The short name of the feature
	 * @param featureJson The feature's JSON settings to modify
	 * @param appliedOverrides Reference to tracking data for applied overrides
	 * @return Number of new overrides applied
	 */
	size_t ApplyNewFeatureOverrides(const std::string& featureName, json& featureJson, json& appliedOverrides);

	/**
	 * @brief Applies global overrides to the main settings JSON
	 * @param mainJson The main settings JSON to modify
	 * @return Number of global overrides applied
	 */
	size_t ApplyGlobalOverrides(json& mainJson);

	/**
	 * @brief Gets list of all discovered overrides
	 * @return Vector of override information
	 */
	const std::vector<OverrideInfo>& GetOverrides() const { return overrides; }

	/**
	 * @brief Gets overrides for a specific feature
	 * @param featureName The short name of the feature
	 * @return Vector of override information for the feature
	 */
	std::vector<const OverrideInfo*> GetFeatureOverrides(const std::string& featureName) const;

	/**
	 * @brief Checks if there are any overrides available for a specific feature
	 * @param featureName The short name of the feature
	 * @return True if the feature has overrides available
	 */
	bool HasFeatureOverrides(const std::string& featureName) const;

	/**
	 * @brief Manually reapplies all overrides for a specific feature to the provided JSON
	 * @param featureName The short name of the feature
	 * @param featureJson JSON object to apply overrides to
	 * @return Number of overrides applied
	 */
	size_t ReapplyFeatureOverrides(const std::string& featureName, json& featureJson);

	/**
	 * @brief Enables or disables a specific override
	 * @param modName Name of the mod
	 * @param featureName Feature name (empty for global)
	 * @param isEnabled Whether to enable the override
	 */
	void SetOverrideEnabled(const std::string& modName, const std::string& featureName, bool isEnabled);

	/**
	 * @brief Clears all cached overrides and forces rediscovery
	 */
	void RefreshOverrides();

	/**
	 * @brief Gets the overrides directory path
	 */
	std::filesystem::path GetOverridesDirectory() const;

	/**
	 * @brief Loads applied overrides tracking data
	 * @return JSON object containing tracking data for previously applied overrides
	 */
	json LoadAppliedOverridesTracking() const;

	/**
	 * @brief Saves applied overrides tracking data
	 * @param appliedOverrides JSON object containing tracking data
	 */
	void SaveAppliedOverridesTracking(const json& appliedOverrides) const;

	/**
	 * @brief Gets the path to the applied overrides tracking file
	 */
	std::filesystem::path GetAppliedOverridesTrackingPath() const;

	/**
	 * @brief Checks if override system is enabled
	 */
	bool IsEnabled() const { return enabled; }

	/**
	 * @brief Enables or disables the entire override system
	 */
	void SetEnabled(bool enable) { enabled = enable; }

	/**
	 * @brief Reports an override failure to the Feature Issues system
	 * @param modName Name of the mod
	 * @param featureName Feature name (empty for global overrides)
	 * @param errorMessage Description of the failure
	 */
	void ReportOverrideFailure(const std::string& modName, const std::string& featureName, const std::string& errorMessage);

private:
	SettingsOverrideManager() = default;
	~SettingsOverrideManager() = default;
	SettingsOverrideManager(const SettingsOverrideManager&) = delete;
	SettingsOverrideManager& operator=(const SettingsOverrideManager&) = delete;

	/**
	 * @brief Loads a single override file
	 * @param filePath Path to the override file
	 * @return Override info if successful, nullptr otherwise
	 */
	std::unique_ptr<OverrideInfo> LoadOverrideFile(const std::filesystem::path& filePath);

	/**
	 * @brief Parses mod name and feature name from filename
	 * @param filename The override filename
	 * @return Pair of {modName, featureName} (featureName empty for global)
	 */
	std::pair<std::string, std::string> ParseOverrideFilename(const std::string& filename);

	/**
	 * @brief Validates override file format and content
	 * @param overrideJson The JSON to validate
	 * @param filePath Path to the file being validated (for error reporting)
	 * @return True if valid
	 */
	bool ValidateOverrideFormat(const json& overrideJson, const std::string& filePath = "");

	/**
	 * @brief Validates JSON data types and ranges for safety
	 * @param jsonData The JSON data to validate
	 * @param path Current path in the JSON (for error reporting)
	 * @param filePath Path to the file being validated (for error reporting)
	 * @return True if all data types are safe
	 */
	bool ValidateJsonDataTypes(const json& jsonData, const std::string& path = "", const std::string& filePath = "");

	/**
	 * @brief Sanitizes JSON data to prevent corruption
	 * @param jsonData The JSON data to sanitize
	 * @return Sanitized JSON data
	 */
	json SanitizeJsonData(const json& jsonData);

	/**
	 * @brief Recursively merges override JSON into target JSON
	 * @param target The target JSON to modify
	 * @param override The override JSON to apply
	 */
	void MergeJson(json& target, const json& override);

	std::vector<OverrideInfo> overrides;
	std::unordered_map<std::string, std::vector<size_t>> featureOverrideMap;  // Maps feature name to override indices
	bool enabled = true;
	bool discovered = false;

	static constexpr const char* OVERRIDES_DIR = "Data\\SKSE\\Plugins\\CommunityShaders\\Overrides";
	static constexpr const char* GLOBAL_SUFFIX = "_Global.json";
	static constexpr const char* APPLIED_OVERRIDES_TRACKING_FILE = "Data\\SKSE\\Plugins\\CommunityShaders\\AppliedOverrides.json";
	
	// Security limits for JSON validation
	static constexpr size_t MAX_JSON_DEPTH = 10;
	static constexpr size_t MAX_STRING_LENGTH = 1000;
	static constexpr size_t MAX_ARRAY_SIZE = 100;
	static constexpr size_t MAX_OBJECT_SIZE = 100;
	static constexpr double MAX_NUMERIC_VALUE = 1e6;
	static constexpr double MIN_NUMERIC_VALUE = -1e6;
};
