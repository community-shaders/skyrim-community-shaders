#pragma once

#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

/**
 * @brief Loads the Baseline folders of enabled preset packs and layers them over feature defaults.
 *
 * Each pack's `Baseline/<FeatureShortName>.json` sets part of one feature's settings; packs apply in
 * layering order, so a later pack wins conflicts. The user's own edits sit above this layer (see State).
 */
class BaselineManager
{
public:
	struct BaselineFile
	{
		/// Presets pack the file belongs to.
		std::string packId;
		std::string featureName;
		/// UTF-8; turn it back into a path with Util::PathHelpers::FromUtf8.
		std::string filePath;
		json values;

		// Metadata from the file
		std::string version;
		std::string description;
		/// `_metadata.enabled`; a disabled file is skipped at discovery.
		bool enabled = true;

		// Keys the feature does not serialize; while non-empty the file is not applied
		std::vector<std::string> unknownKeys;
	};

	/** @brief Gets the singleton instance */
	static BaselineManager* GetSingleton()
	{
		static BaselineManager instance;
		return &instance;
	}

	/**
	 * @brief Discovers the Baseline folder of each enabled preset pack, in layering order
	 * @return Number of files discovered
	 */
	size_t DiscoverFiles();

	/**
	 * @brief The feature a Baseline file targets: the part of the file stem after the last underscore, or the whole stem
	 * @return Empty for non-JSON, hidden or temporary files
	 */
	static std::string ParseBaselineFeatureName(const std::filesystem::path& filePath);

	/**
	 * @brief Applies the enabled Baseline files to a feature's settings JSON
	 * @param featureName The short name of the feature
	 * @param featureJson The feature's JSON settings to modify; when non-empty it is taken as the
	 *        canonical shape, and files naming keys absent from it are skipped and reported
	 * @return Number of files applied
	 */
	size_t ApplyBaselines(const std::string& featureName, json& featureJson);

	/**
	 * @brief Gets list of all discovered Baseline files
	 * @return Discovered files, in layering order
	 */
	const std::vector<BaselineFile>& GetFiles() const { return files; }

	/** @brief Bumped on every discovery, so callers can cache what they derive from GetFiles(). */
	std::uint64_t GetRevision() const { return revision; }

	/**
	 * @brief Gets the Baseline files for a specific feature
	 * @param featureName The short name of the feature
	 * @return The feature's files, in layering order
	 */
	std::vector<const BaselineFile*> GetFeatureFiles(const std::string& featureName) const;

	/**
	 * @brief Checks if there are any Baseline files for a specific feature
	 * @param featureName The short name of the feature
	 * @return True if the feature has Baseline files
	 */
	bool HasFeatureFiles(const std::string& featureName) const;

	/**
	 * @brief Clears all cached files and forces rediscovery
	 */
	void Refresh();

	/**
	 * @brief Reports a Baseline file failure to the Feature Issues system
	 * @param packId Pack the file belongs to
	 * @param featureName Feature the file targets
	 * @param errorMessage Description of the failure
	 * @param filePath The file, for the issue's file details; may be empty
	 */
	void ReportFailure(const std::string& packId, const std::string& featureName, const std::string& errorMessage,
		const std::filesystem::path& filePath = {});

	/**
	 * @brief The Baseline file ExportSettings writes: `Presets/<packName>/Baseline/<Feature>.json`
	 * @return Empty when packName sanitizes to nothing
	 */
	std::filesystem::path GetExportDestination(const std::string& packName, const std::string& featureName) const;

	/** @brief Reports whether a file can reach a feature that persists settings */
	bool IsApplicable(const BaselineFile& info) const;

	/**
	 * @brief Reads an export destination raw, keeping its _metadata
	 * @return An empty object when the file does not exist yet, nullopt when it exists but cannot be read
	 */
	static std::optional<json> ReadExportTarget(const std::filesystem::path& destination);

	/**
	 * @brief Writes selected feature settings to a pack's Baseline file, merging into an existing one and
	 *        creating a starter manifest for a new pack
	 * @param packName Pack folder name; sanitized before use
	 * @param settingPaths JSON pointers into featureSettings, as reported by Util::Settings::GetExportSettings
	 * @param removePaths Settings to drop from the existing file, in the same form as settingPaths
	 * @return True if the file was written. The discovered files are not refreshed: only enabled packs are
	 *         discovered, and UnifiedPresetCatalog::ReapplyIfInUse refreshes those.
	 */
	bool ExportSettings(const std::string& packName, const std::string& featureName,
		std::span<const std::string> settingPaths, const json& featureSettings, std::span<const std::string> removePaths = {});

	/**
	 * @brief Whether a document passes the format and data checks every Baseline file is loaded with,
	 * for writers that produce whole files rather than going through ExportSettings
	 * @param filePath Destination, named in the log when a check fails
	 */
	bool IsValidDocument(const json& document, const std::filesystem::path& filePath);

private:
	BaselineManager() = default;
	~BaselineManager() = default;
	BaselineManager(const BaselineManager&) = delete;
	BaselineManager& operator=(const BaselineManager&) = delete;

	/**
	 * @brief Loads a single Baseline file
	 * @param filePath Path to the file
	 * @param packId Presets pack the file belongs to; the file must target a known feature
	 * @return File info if successful, nullptr otherwise
	 */
	std::unique_ptr<BaselineFile> LoadFile(const std::filesystem::path& filePath, const std::string& packId);

	/**
	 * @brief Loads every file in one pack's Baseline folder into the discovered set
	 * @param directory Folder to scan (not recursive)
	 * @param packId Presets pack id
	 * @return Number of files processed, counted against the per-scan safety limit
	 */
	size_t DiscoverDirectory(const std::filesystem::path& directory, const std::string& packId);

	/**
	 * @brief Validates Baseline file format and content
	 * @param fileJson The JSON to validate
	 * @param filePath Path to the file being validated (for error reporting)
	 * @return True if valid
	 */
	bool ValidateFormat(const json& fileJson, const std::string& filePath = "");

	/**
	 * @brief Validates JSON data types and ranges for safety
	 * @param jsonData The JSON data to validate
	 * @param path Current path in the JSON (for error reporting)
	 * @param filePath Path to the file being validated (for error reporting)
	 * @param depth Nesting of jsonData below the document root, objects and arrays alike
	 * @return True if all data types are safe
	 */
	bool ValidateJsonDataTypes(const json& jsonData, const std::string& path = "", const std::string& filePath = "", size_t depth = 0);

	/**
	 * @brief Sanitizes JSON data to prevent corruption
	 * @param jsonData The JSON data to sanitize
	 * @return Sanitized JSON data
	 */
	json SanitizeJsonData(const json& jsonData);

	/**
	 * @brief Reports whether a file names keys the feature does not serialize.
	 * @param file The file to check; its unknownKeys verdict is refreshed when featureJson has a shape
	 * @param featureJson Canonical settings blob, or an empty object to reuse the last verdict
	 * @return True if the file must not be applied
	 */
	bool RejectsUnknownKeys(BaselineFile& file, const json& featureJson);

	std::vector<BaselineFile> files;
	std::unordered_map<std::string, std::vector<size_t>> featureFileMap;  // Maps feature name to file indices
	bool discovered = false;
	std::uint64_t revision = 0;

	// Security limits for JSON validation
	static constexpr size_t MAX_JSON_DEPTH = 10;
	static constexpr size_t MAX_STRING_LENGTH = 1000;
	static constexpr size_t MAX_ARRAY_SIZE = 100;
	static constexpr size_t MAX_OBJECT_SIZE = 100;
	static constexpr double MAX_NUMERIC_VALUE = 1e6;
	static constexpr double MIN_NUMERIC_VALUE = -1e6;
};
