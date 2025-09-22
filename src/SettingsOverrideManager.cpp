#include "SettingsOverrideManager.h"

#include "FeatureIssues.h"
#include "Utils/FileSystem.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

using namespace SKSE;

namespace
{
	// Simple hash function for file contents
	std::string ComputeContentHash(const std::string& content)
	{
		std::hash<std::string> hasher;
		auto hash = hasher(content);
		std::ostringstream oss;
		oss << std::hex << hash;
		return oss.str();
	}
}

size_t SettingsOverrideManager::DiscoverOverrides()
{
	if (!enabled) {
		return 0;
	}

	overrides.clear();
	featureOverrideMap.clear();

	auto overridesDir = GetOverridesDirectory();

	if (!std::filesystem::exists(overridesDir)) {
		logger::info("Overrides directory does not exist: {}", overridesDir.string());
		discovered = true;
		return 0;
	}

	logger::info("Discovering override files in: {}", overridesDir.string());

	size_t filesProcessed = 0;
	size_t filesLoaded = 0;
	size_t maxFilesToProcess = 1000;  // Prevent processing too many files

	try {
		for (const auto& entry : std::filesystem::directory_iterator(overridesDir)) {
			// Safety check to prevent infinite loops or DoS
			if (filesProcessed >= maxFilesToProcess) {
				logger::info("Reached maximum file processing limit ({}), stopping discovery", maxFilesToProcess);
				break;
			}
			filesProcessed++;

			// Skip if not a regular file or not a JSON file
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}

			// Check filename length to prevent overly long names
			std::string filename = entry.path().filename().string();
			if (filename.length() > MAX_STRING_LENGTH) {
				logger::info("Skipping override file with overly long name: {}", filename.substr(0, 50) + "...");
				continue;
			}

			// Skip hidden files or files starting with special characters
			if (filename.empty() || filename[0] == '.' || filename[0] == '~') {
				logger::info("Skipping hidden/temporary file: {}", filename);
				continue;
			}

			std::error_code ec;
			auto fileSize = std::filesystem::file_size(entry.path(), ec);
			if (ec) {
				logger::info("Could not get size of override file, skipping: {}", entry.path().string());
				continue;
			}

			// Skip empty files or overly large files
			if (fileSize == 0) {
				logger::info("Skipping empty override file: {}", entry.path().string());
				continue;
			}

			constexpr size_t MAX_OVERRIDE_FILE_SIZE = 1024 * 1024;  // 1MB
			if (fileSize > MAX_OVERRIDE_FILE_SIZE) {
				logger::info("Skipping overly large override file ({}KB): {}", fileSize / 1024, entry.path().string());
				continue;
			}

			try {
				auto overrideInfo = LoadOverrideFile(entry.path());
				if (overrideInfo) {
					size_t index = overrides.size();
					overrides.push_back(std::move(*overrideInfo));

					// Map feature overrides for quick lookup
					const auto& override = overrides[index];
					if (!override.isGlobal) {
						// Validate feature name before mapping
						if (!override.featureName.empty() && override.featureName.length() <= MAX_STRING_LENGTH) {
							featureOverrideMap[override.featureName].push_back(index);
						} else {
							logger::info("Override has invalid feature name, skipping feature mapping: {}", override.modName);
						}
					}

					filesLoaded++;
					logger::info("Loaded override: {} for {}",
						override.modName,
						override.isGlobal ? "Global" : override.featureName);
				} else {
					// LoadOverrideFile returned nullptr, parse filename to report error
					auto [modName, featureName] = ParseOverrideFilename(entry.path().filename().string());
					if (!modName.empty()) {
						ReportOverrideFailure(modName, featureName, "File could not be loaded or parsed");
					}
				}
			} catch (const std::exception& e) {
				logger::info("Error loading override file {}: {}", entry.path().string(), e.what());

				// Report to Feature Issues
				auto [modName, featureName] = ParseOverrideFilename(entry.path().filename().string());
				if (!modName.empty()) {
					ReportOverrideFailure(modName, featureName, e.what());
				}
				continue;
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::info("Error accessing overrides directory: {}", e.what());
	} catch (const std::exception& e) {
		logger::info("Unexpected error during override discovery: {}", e.what());
	}

	discovered = true;
	logger::info("Discovered {} override files ({} processed)", filesLoaded, filesProcessed);
	return overrides.size();
}

size_t SettingsOverrideManager::ApplyOverrides(const std::string& featureName, json& featureJson)
{
	if (!enabled || !discovered) {
		return 0;
	}

	size_t appliedCount = 0;

	auto it = featureOverrideMap.find(featureName);
	if (it != featureOverrideMap.end()) {
		for (size_t index : it->second) {
			const auto& override = overrides[index];
			if (override.enabled) {
				try {
					MergeJson(featureJson, override.overrideData);
					appliedCount++;
					logger::info("Applied override from {} to {}", override.modName, featureName);
				} catch (const std::exception& e) {
					logger::info("Failed to apply override from {} to {}: {}",
						override.modName, featureName, e.what());

					// Report application failure to Feature Issues
					ReportOverrideFailure(override.modName, featureName, "Failed to apply override: " + std::string(e.what()));
				}
			}
		}
	}

	return appliedCount;
}

size_t SettingsOverrideManager::ApplyGlobalOverrides(json& mainJson)
{
	if (!enabled || !discovered) {
		return 0;
	}

	size_t appliedCount = 0;

	for (const auto& override : overrides) {
		if (override.isGlobal && override.enabled) {
			try {
				MergeJson(mainJson, override.overrideData);
				appliedCount++;
				logger::info("Applied global override from {}", override.modName);
			} catch (const std::exception& e) {
				logger::info("Failed to apply global override from {}: {}",
					override.modName, e.what());

				// Report application failure to Feature Issues
				ReportOverrideFailure(override.modName, "", "Failed to apply global override: " + std::string(e.what()));
			}
		}
	}

	return appliedCount;
}

std::vector<const SettingsOverrideManager::OverrideInfo*> SettingsOverrideManager::GetFeatureOverrides(const std::string& featureName) const
{
	std::vector<const OverrideInfo*> result;

	auto it = featureOverrideMap.find(featureName);
	if (it != featureOverrideMap.end()) {
		for (size_t index : it->second) {
			result.push_back(&overrides[index]);
		}
	}

	return result;
}

bool SettingsOverrideManager::HasFeatureOverrides(const std::string& featureName) const
{
	if (!enabled || !discovered) {
		return false;
	}

	auto it = featureOverrideMap.find(featureName);
	return it != featureOverrideMap.end() && !it->second.empty();
}

size_t SettingsOverrideManager::ReapplyFeatureOverrides(const std::string& featureName, json& featureJson)
{
	if (!enabled || !discovered) {
		return 0;
	}

	size_t appliedCount = 0;

	auto it = featureOverrideMap.find(featureName);
	if (it != featureOverrideMap.end()) {
		for (size_t index : it->second) {
			const auto& override = overrides[index];
			if (override.enabled) {
				try {
					MergeJson(featureJson, override.overrideData);
					appliedCount++;
					logger::info("Manually reapplied override from {} to {}", override.modName, featureName);
				} catch (const std::exception& e) {
					logger::warn("Failed to manually reapply override from {} to {}: {}",
						override.modName, featureName, e.what());

					// Report reapplication failure to Feature Issues
					ReportOverrideFailure(override.modName, featureName, "Failed to reapply override: " + std::string(e.what()));
				}
			}
		}
	}

	return appliedCount;
}

void SettingsOverrideManager::SetOverrideEnabled(const std::string& modName, const std::string& featureName, bool isEnabled)
{
	for (auto& override : overrides) {
		if (override.modName == modName &&
			((featureName.empty() && override.isGlobal) || override.featureName == featureName)) {
			override.enabled = isEnabled;
			logger::info("{} override from {} for {}",
				isEnabled ? "Enabled" : "Disabled",
				modName,
				featureName.empty() ? "Global" : featureName);
			break;
		}
	}
}

void SettingsOverrideManager::RefreshOverrides()
{
	discovered = false;
	DiscoverOverrides();
}

std::filesystem::path SettingsOverrideManager::GetOverridesDirectory() const
{
	return Util::PathHelpers::GetOverridesPath();
}

json SettingsOverrideManager::LoadAppliedOverridesTracking() const
{
	json appliedOverrides;
	try {
		auto trackingPath = GetAppliedOverridesTrackingPath();

		// Check if file exists and is reasonable size
		std::error_code ec;
		if (!std::filesystem::exists(trackingPath, ec)) {
			logger::info("Applied overrides tracking file does not exist yet: {}", trackingPath.string());
			return appliedOverrides;
		}

		auto fileSize = std::filesystem::file_size(trackingPath, ec);
		if (ec) {
			logger::info("Could not get size of applied overrides tracking file: {}", trackingPath.string());
			return appliedOverrides;
		}

		// Limit tracking file size to prevent abuse
		constexpr size_t MAX_TRACKING_FILE_SIZE = 10 * 1024 * 1024;  // 10MB
		if (fileSize > MAX_TRACKING_FILE_SIZE) {
			logger::info("Applied overrides tracking file too large ({}KB), ignoring: {}", fileSize / 1024, trackingPath.string());
			return appliedOverrides;
		}

		std::ifstream file(trackingPath);
		if (file.is_open()) {
			try {
				file >> appliedOverrides;

				// Validate the loaded JSON structure
				if (!appliedOverrides.is_object()) {
					logger::info("Applied overrides tracking file contains invalid data structure, resetting");
					appliedOverrides = json::object();
				} else {
					// Validate each tracking entry
					auto it = appliedOverrides.begin();
					while (it != appliedOverrides.end()) {
						const std::string& key = it.key();
						const auto& value = it.value();

						// Validate tracking entry structure
						if (!value.is_object() ||
							!value.contains("hash") ||
							!value.contains("firstApplied") ||
							!value["hash"].is_string() ||
							!value["firstApplied"].is_number()) {
							logger::info("Invalid tracking entry for '{}', removing", key);
							it = appliedOverrides.erase(it);
						} else {
							++it;
						}
					}
				}
			} catch (const json::parse_error& e) {
				logger::info("Parse error in applied overrides tracking file: {} at byte {}", e.what(), e.byte);
				appliedOverrides = json::object();
			} catch (const json::exception& e) {
				logger::info("JSON error reading applied overrides tracking file: {}", e.what());
				appliedOverrides = json::object();
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::info("Filesystem error loading applied overrides tracking: {}", e.what());
	} catch (const std::exception& e) {
		logger::info("Error loading applied overrides tracking: {}", e.what());
	}
	return appliedOverrides;
}

void SettingsOverrideManager::SaveAppliedOverridesTracking(const json& appliedOverrides) const
{
	try {
		// Validate input data
		if (!appliedOverrides.is_object()) {
			logger::info("Cannot save applied overrides tracking - invalid data type");
			return;
		}

		auto trackingPath = GetAppliedOverridesTrackingPath();

		// Create directory if it doesn't exist
		std::filesystem::create_directories(trackingPath.parent_path());

		// Create a backup of existing file before overwriting
		std::error_code ec;
		if (std::filesystem::exists(trackingPath, ec) && !ec) {
			auto backupPath = trackingPath;
			backupPath += ".backup";
			std::filesystem::copy_file(trackingPath, backupPath,
				std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				logger::info("Could not create backup of tracking file: {}", ec.message());
			}
		}

		std::ofstream file(trackingPath);
		if (file.is_open()) {
			try {
				file << appliedOverrides.dump(1);
				file.flush();

				if (file.fail()) {
					logger::info("Failed to write applied overrides tracking file completely");
				}
			} catch (const json::exception& e) {
				logger::info("JSON error writing applied overrides tracking file: {}", e.what());
			}
		} else {
			logger::info("Could not open applied overrides tracking file for writing: {}", trackingPath.string());
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::info("Filesystem error saving applied overrides tracking: {}", e.what());
	} catch (const std::exception& e) {
		logger::info("Error saving applied overrides tracking: {}", e.what());
	}
}

std::filesystem::path SettingsOverrideManager::GetAppliedOverridesTrackingPath() const
{
	return Util::PathHelpers::GetAppliedOverridesPath();
}

size_t SettingsOverrideManager::ApplyNewOverrides(json& baseSettings, json& appliedOverrides)
{
	if (!enabled || !discovered) {
		return 0;
	}

	// Validate input parameters
	if (!baseSettings.is_object()) {
		logger::info("Cannot apply overrides - base settings is not a JSON object");
		return 0;
	}

	if (!appliedOverrides.is_object()) {
		logger::info("Applied overrides tracking data is not a JSON object, resetting");
		appliedOverrides = json::object();
	}

	size_t appliedCount = 0;
	auto currentTime = std::time(nullptr);

	for (const auto& override : overrides) {
		if (!override.enabled || !override.isGlobal) {
			continue;
		}

		try {
			// Create tracking key
			std::string trackingKey = override.modName + "_Global";

			// Validate tracking key length
			if (trackingKey.length() > MAX_STRING_LENGTH) {
				logger::info("Skipping override with overly long tracking key: {}", override.modName);
				continue;
			}

			// Check if this override has been applied before
			bool shouldApply = false;
			if (!appliedOverrides.contains(trackingKey)) {
				// First time seeing this override
				shouldApply = true;
			} else {
				// Check if the override file has changed
				auto& tracking = appliedOverrides[trackingKey];
				if (!tracking.is_object()) {
					// Invalid tracking data, reapply
					shouldApply = true;
					logger::info("Invalid tracking data for {}, reapplying", override.modName);
				} else {
					std::string currentHash = tracking.value("hash", "");
					if (currentHash != override.fileHash) {
						// Override file has changed, reapply
						shouldApply = true;
						logger::info("Override file {} has changed, reapplying", override.modName);
					}
				}
			}

			if (shouldApply) {
				// Validate override data before merging
				if (!override.overrideData.is_object()) {
					logger::info("Skipping override from {} - invalid data structure", override.modName);
					continue;
				}

				// Create a backup of base settings for rollback if needed
				json backupSettings = baseSettings;

				try {
					MergeJson(baseSettings, override.overrideData);
					appliedCount++;

					// Update tracking
					appliedOverrides[trackingKey] = {
						{ "hash", override.fileHash },
						{ "firstApplied", currentTime },
						{ "lastApplied", currentTime },
						{ "version", override.version }
					};

					logger::info("Applied global override from {}", override.modName);
				} catch (const std::exception& mergeError) {
					// Rollback on merge failure
					baseSettings = backupSettings;
					logger::info("Failed to merge global override from {}, rolled back: {}", override.modName, mergeError.what());
				}
			}
		} catch (const std::exception& e) {
			logger::info("Error processing global override from {}: {}", override.modName, e.what());
		}
	}

	return appliedCount;
}

size_t SettingsOverrideManager::ApplyNewFeatureOverrides(const std::string& featureName, json& featureJson, json& appliedOverrides)
{
	if (!enabled || !discovered) {
		return 0;
	}

	// Validate input parameters
	if (featureName.empty() || featureName.length() > MAX_STRING_LENGTH) {
		logger::info("Cannot apply overrides - invalid feature name");
		return 0;
	}

	if (!featureJson.is_object()) {
		logger::info("Cannot apply overrides to {} - feature JSON is not an object", featureName);
		return 0;
	}

	if (!appliedOverrides.is_object()) {
		logger::info("Applied overrides tracking data is not a JSON object, resetting");
		appliedOverrides = json::object();
	}

	size_t appliedCount = 0;
	auto currentTime = std::time(nullptr);

	auto it = featureOverrideMap.find(featureName);
	if (it != featureOverrideMap.end()) {
		for (size_t index : it->second) {
			if (index >= overrides.size()) {
				logger::info("Invalid override index {} for feature {}", index, featureName);
				continue;
			}

			const auto& override = overrides[index];
			if (!override.enabled) {
				continue;
			}

			try {
				// Create tracking key
				std::string trackingKey = override.modName + "_" + featureName;

				// Validate tracking key length
				if (trackingKey.length() > MAX_STRING_LENGTH) {
					logger::info("Skipping override with overly long tracking key: {}_{}", override.modName, featureName);
					continue;
				}

				// Check if this override has been applied before
				bool shouldApply = false;
				if (!appliedOverrides.contains(trackingKey)) {
					// First time seeing this override
					shouldApply = true;
				} else {
					// Check if the override file has changed
					auto& tracking = appliedOverrides[trackingKey];
					if (!tracking.is_object()) {
						// Invalid tracking data, reapply
						shouldApply = true;
						logger::info("Invalid tracking data for {} on {}, reapplying", override.modName, featureName);
					} else {
						std::string currentHash = tracking.value("hash", "");
						if (currentHash != override.fileHash) {
							// Override file has changed, reapply
							shouldApply = true;
							logger::info("Override file {} for {} has changed, reapplying", override.modName, featureName);
						}
					}
				}

				if (shouldApply) {
					// Validate override data before merging
					if (!override.overrideData.is_object()) {
						logger::info("Skipping override from {} for {} - invalid data structure", override.modName, featureName);
						continue;
					}

					// Create a backup of feature settings for rollback if needed
					json backupSettings = featureJson;

					try {
						MergeJson(featureJson, override.overrideData);
						appliedCount++;

						// Update tracking
						appliedOverrides[trackingKey] = {
							{ "hash", override.fileHash },
							{ "firstApplied", currentTime },
							{ "lastApplied", currentTime },
							{ "version", override.version }
						};

						logger::info("Applied override from {} to {}", override.modName, featureName);
					} catch (const std::exception& mergeError) {
						// Rollback on merge failure
						featureJson = backupSettings;
						logger::info("Failed to merge override from {} to {}, rolled back: {}", override.modName, featureName, mergeError.what());
					}
				}
			} catch (const std::exception& e) {
				logger::info("Error processing override from {} for {}: {}", override.modName, featureName, e.what());
			}
		}
	}

	return appliedCount;
}

std::unique_ptr<SettingsOverrideManager::OverrideInfo> SettingsOverrideManager::LoadOverrideFile(const std::filesystem::path& filePath)
{
	try {
		// Check file size to prevent loading extremely large files
		std::error_code ec;
		auto fileSize = std::filesystem::file_size(filePath, ec);
		if (ec) {
			logger::info("Could not get file size for override file: {}", filePath.string());
			return nullptr;
		}

		// Limit file size to 1MB to prevent abuse
		constexpr size_t MAX_OVERRIDE_FILE_SIZE = 1024 * 1024;
		if (fileSize > MAX_OVERRIDE_FILE_SIZE) {
			logger::info("Override file too large ({}KB, max 1MB): {}", fileSize / 1024, filePath.string());
			return nullptr;
		}

		std::ifstream file(filePath);
		if (!file.is_open()) {
			logger::info("Could not open override file: {}", filePath.string());
			return nullptr;
		}

		// Read entire file content for hash computation
		std::string fileContent;
		fileContent.reserve(static_cast<size_t>(fileSize));
		fileContent.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		// Validate file content is not empty
		if (fileContent.empty()) {
			logger::info("Override file is empty: {}", filePath.string());
			return nullptr;
		}

		// Parse JSON from content with error handling
		json overrideJson;
		try {
			overrideJson = json::parse(fileContent);
		} catch (const json::parse_error& e) {
			logger::info("JSON parse error in override file {}: {} at byte {}",
				filePath.string(), e.what(), e.byte);
			return nullptr;
		} catch (const json::exception& e) {
			logger::info("JSON error in override file {}: {}", filePath.string(), e.what());
			return nullptr;
		}

		// Validate override format and data types
		if (!ValidateOverrideFormat(overrideJson, filePath.string())) {
			logger::info("Invalid override file format: {}", filePath.string());
			return nullptr;
		}

		if (!ValidateJsonDataTypes(overrideJson, "", filePath.string())) {
			logger::info("Invalid data types in override file: {}", filePath.string());
			return nullptr;
		}

		auto overrideInfo = std::make_unique<OverrideInfo>();

		auto [modName, featureName] = ParseOverrideFilename(filePath.filename().string());

		// Validate mod name and feature name
		if (modName.empty() || modName.length() > MAX_STRING_LENGTH) {
			logger::info("Invalid mod name in override file: {}", filePath.string());
			return nullptr;
		}

		if (!featureName.empty() && featureName.length() > MAX_STRING_LENGTH) {
			logger::info("Invalid feature name in override file: {}", filePath.string());
			return nullptr;
		}

		overrideInfo->modName = modName;
		overrideInfo->featureName = featureName;
		overrideInfo->filePath = filePath.string();
		overrideInfo->isGlobal = featureName.empty();
		overrideInfo->fileHash = ComputeContentHash(fileContent);

		// Extract and validate metadata if present
		if (overrideJson.contains("_metadata") && overrideJson["_metadata"].is_object()) {
			const auto& metadata = overrideJson["_metadata"];

			try {
				if (metadata.contains("version") && metadata["version"].is_string()) {
					std::string version = metadata["version"];
					if (version.length() <= MAX_STRING_LENGTH) {
						overrideInfo->version = version;
					} else {
						logger::info("Version string too long in override file: {}", filePath.string());
					}
				}

				if (metadata.contains("description") && metadata["description"].is_string()) {
					std::string description = metadata["description"];
					if (description.length() <= MAX_STRING_LENGTH) {
						overrideInfo->description = description;
					} else {
						logger::info("Description string too long in override file: {}", filePath.string());
					}
				}

				if (metadata.contains("enabled") && metadata["enabled"].is_boolean()) {
					overrideInfo->enabled = metadata["enabled"];
				}
			} catch (const json::exception& e) {
				logger::info("Error parsing metadata in override file {}: {}", filePath.string(), e.what());
				// Continue without metadata if it's malformed
			}
		}

		// Store the override data (excluding metadata) and sanitize it
		overrideInfo->overrideData = SanitizeJsonData(overrideJson);
		if (overrideInfo->overrideData.contains("_metadata")) {
			overrideInfo->overrideData.erase("_metadata");
		}

		return overrideInfo;

	} catch (const std::filesystem::filesystem_error& e) {
		logger::info("Filesystem error loading override file {}: {}", filePath.string(), e.what());
		return nullptr;
	} catch (const std::exception& e) {
		logger::info("Unexpected error loading override file {}: {}", filePath.string(), e.what());
		return nullptr;
	}
}

std::pair<std::string, std::string> SettingsOverrideManager::ParseOverrideFilename(const std::string& filename)
{
	// Remove .json extension
	std::string nameWithoutExt = filename;
	const std::string jsonExt = ".json";
	if (nameWithoutExt.length() >= jsonExt.length() &&
		nameWithoutExt.substr(nameWithoutExt.length() - jsonExt.length()) == jsonExt) {
		nameWithoutExt = nameWithoutExt.substr(0, nameWithoutExt.length() - jsonExt.length());
	}

	// Check for global override
	const std::string globalSuffix = "_Global";
	if (nameWithoutExt.length() >= globalSuffix.length() &&
		nameWithoutExt.substr(nameWithoutExt.length() - globalSuffix.length()) == globalSuffix) {
		std::string modName = nameWithoutExt.substr(0, nameWithoutExt.length() - globalSuffix.length());
		return { modName, "" };  // Empty feature name indicates global
	}

	// Parse ModName_FeatureName format
	size_t lastUnderscore = nameWithoutExt.find_last_of('_');
	if (lastUnderscore != std::string::npos && lastUnderscore > 0) {
		std::string modName = nameWithoutExt.substr(0, lastUnderscore);
		std::string featureName = nameWithoutExt.substr(lastUnderscore + 1);
		return { modName, featureName };
	}

	// Fallback: treat entire name as mod name with no specific feature
	return { nameWithoutExt, "" };
}

bool SettingsOverrideManager::ValidateOverrideFormat(const json& overrideJson, const std::string& filePath)
{
	try {
		// Basic validation - must be an object
		if (!overrideJson.is_object()) {
			if (!filePath.empty()) {
				logger::info("Override file must contain a JSON object: {}", filePath);
			}
			return false;
		}

		// Check object size limits
		if (overrideJson.size() > MAX_OBJECT_SIZE) {
			if (!filePath.empty()) {
				logger::info("Override file contains too many top-level properties (max {}): {}", MAX_OBJECT_SIZE, filePath);
			}
			return false;
		}

		// Must have at least one non-metadata field
		bool hasNonMetadata = false;
		for (const auto& [key, value] : overrideJson.items()) {
			if (key.length() == 0 || key[0] != '_') {
				hasNonMetadata = true;
				break;
			}
		}

		if (!hasNonMetadata) {
			if (!filePath.empty()) {
				logger::info("Override file contains only metadata, no actual overrides: {}", filePath);
			}
			return false;
		}

		// Validate metadata section if present
		if (overrideJson.contains("_metadata")) {
			const auto& metadata = overrideJson["_metadata"];
			if (!metadata.is_object()) {
				if (!filePath.empty()) {
					logger::info("_metadata must be an object in override file: {}", filePath);
				}
				return false;
			}

			// Validate metadata fields
			for (const auto& [key, value] : metadata.items()) {
				if (key == "version" || key == "description") {
					if (!value.is_string()) {
						if (!filePath.empty()) {
							logger::info("Metadata field '{}' must be a string in override file: {}", key, filePath);
						}
						return false;
					}
					if (value.get<std::string>().length() > MAX_STRING_LENGTH) {
						if (!filePath.empty()) {
							logger::info("Metadata field '{}' too long (max {} chars) in override file: {}", key, MAX_STRING_LENGTH, filePath);
						}
						return false;
					}
				} else if (key == "enabled") {
					if (!value.is_boolean()) {
						if (!filePath.empty()) {
							logger::info("Metadata field 'enabled' must be a boolean in override file: {}", filePath);
						}
						return false;
					}
				} else if (key == "modName") {
					// Allow modName in metadata but validate it
					if (!value.is_string()) {
						if (!filePath.empty()) {
							logger::info("Metadata field 'modName' must be a string in override file: {}", filePath);
						}
						return false;
					}
				} else {
					// Unknown metadata field - warn but don't fail
					if (!filePath.empty()) {
						logger::info("Unknown metadata field '{}' in override file: {}", key, filePath);
					}
				}
			}
		}

		return true;

	} catch (const json::exception& e) {
		if (!filePath.empty()) {
			logger::info("JSON error during validation of override file {}: {}", filePath, e.what());
		}
		return false;
	} catch (const std::exception& e) {
		if (!filePath.empty()) {
			logger::info("Error during validation of override file {}: {}", filePath, e.what());
		}
		return false;
	}
}

bool SettingsOverrideManager::ValidateJsonDataTypes(const json& jsonData, const std::string& path, const std::string& filePath)
{
	try {
		// Check nesting depth
		if (std::count(path.begin(), path.end(), '.') > MAX_JSON_DEPTH) {
			if (!filePath.empty()) {
				logger::info("JSON nesting too deep at '{}' (max {}) in override file: {}", path, MAX_JSON_DEPTH, filePath);
			}
			return false;
		}

		if (jsonData.is_object()) {
			if (jsonData.size() > MAX_OBJECT_SIZE) {
				if (!filePath.empty()) {
					logger::info("JSON object too large at '{}' (max {} properties) in override file: {}", path, MAX_OBJECT_SIZE, filePath);
				}
				return false;
			}

			for (const auto& [key, value] : jsonData.items()) {
				// Validate key length
				if (key.length() > MAX_STRING_LENGTH) {
					if (!filePath.empty()) {
						logger::info("JSON property name too long at '{}' (max {} chars) in override file: {}", path + "." + key, MAX_STRING_LENGTH, filePath);
					}
					return false;
				}

				// Recursively validate value
				std::string newPath = path.empty() ? key : path + "." + key;
				if (!ValidateJsonDataTypes(value, newPath, filePath)) {
					return false;
				}
			}
		} else if (jsonData.is_array()) {
			if (jsonData.size() > MAX_ARRAY_SIZE) {
				if (!filePath.empty()) {
					logger::info("JSON array too large at '{}' (max {} elements) in override file: {}", path, MAX_ARRAY_SIZE, filePath);
				}
				return false;
			}

			for (size_t i = 0; i < jsonData.size(); ++i) {
				std::string newPath = path + "[" + std::to_string(i) + "]";
				if (!ValidateJsonDataTypes(jsonData[i], newPath, filePath)) {
					return false;
				}
			}
		} else if (jsonData.is_string()) {
			if (jsonData.get<std::string>().length() > MAX_STRING_LENGTH) {
				if (!filePath.empty()) {
					logger::info("JSON string too long at '{}' (max {} chars) in override file: {}", path, MAX_STRING_LENGTH, filePath);
				}
				return false;
			}
		} else if (jsonData.is_number()) {
			double value = jsonData.get<double>();
			if (value > MAX_NUMERIC_VALUE || value < MIN_NUMERIC_VALUE) {
				if (!filePath.empty()) {
					logger::info("JSON numeric value out of range at '{}' ({} to {}) in override file: {}", path, MIN_NUMERIC_VALUE, MAX_NUMERIC_VALUE, filePath);
				}
				return false;
			}

			// Check for NaN and infinity
			if (std::isnan(value) || std::isinf(value)) {
				if (!filePath.empty()) {
					logger::info("JSON numeric value is NaN or infinity at '{}' in override file: {}", path, filePath);
				}
				return false;
			}
		}
		// Boolean and null values are always valid

		return true;

	} catch (const json::exception& e) {
		if (!filePath.empty()) {
			logger::info("JSON error during data type validation at '{}' in override file {}: {}", path, filePath, e.what());
		}
		return false;
	} catch (const std::exception& e) {
		if (!filePath.empty()) {
			logger::info("Error during data type validation at '{}' in override file {}: {}", path, filePath, e.what());
		}
		return false;
	}
}

json SettingsOverrideManager::SanitizeJsonData(const json& jsonData)
{
	try {
		json sanitized = jsonData;

		// Remove any potentially dangerous keys that start with system prefixes
		std::vector<std::string> dangerousPrefixes = { "__", "System", "Windows", "HKEY_", "C:\\" };

		std::function<void(json&)> sanitizeRecursive = [&](json& data) {
			if (data.is_object()) {
				auto it = data.begin();
				while (it != data.end()) {
					const std::string& key = it.key();

					// Check for dangerous key prefixes
					bool isDangerous = false;
					for (const auto& prefix : dangerousPrefixes) {
						if (key.length() >= prefix.length() &&
							key.substr(0, prefix.length()) == prefix) {
							isDangerous = true;
							break;
						}
					}

					if (isDangerous) {
						logger::info("Removing potentially dangerous key from override: {}", key);
						it = data.erase(it);
					} else {
						// Recursively sanitize the value
						sanitizeRecursive(it.value());
						++it;
					}
				}
			} else if (data.is_array()) {
				for (auto& element : data) {
					sanitizeRecursive(element);
				}
			} else if (data.is_string()) {
				std::string str = data.get<std::string>();

				// Remove any null characters or control characters except common ones
				str.erase(std::remove_if(str.begin(), str.end(), [](char c) {
					return (c >= 0 && c < 32 && c != '\t' && c != '\n' && c != '\r');
				}),
					str.end());

				// Truncate if too long
				if (str.length() > MAX_STRING_LENGTH) {
					str = str.substr(0, MAX_STRING_LENGTH);
					logger::info("Truncated overly long string in override data");
				}

				data = str;
			} else if (data.is_number()) {
				double value = data.get<double>();

				// Clamp to safe ranges
				if (value > MAX_NUMERIC_VALUE) {
					data = MAX_NUMERIC_VALUE;
					logger::info("Clamped numeric value to maximum allowed value");
				} else if (value < MIN_NUMERIC_VALUE) {
					data = MIN_NUMERIC_VALUE;
					logger::info("Clamped numeric value to minimum allowed value");
				} else if (std::isnan(value) || std::isinf(value)) {
					data = 0.0;
					logger::info("Replaced NaN/infinity numeric value with 0");
				}
			}
		};

		sanitizeRecursive(sanitized);
		return sanitized;

	} catch (const std::exception& e) {
		logger::info("Error during JSON sanitization: {}", e.what());
		return json::object();  // Return empty object on error
	}
}

void SettingsOverrideManager::MergeJson(json& target, const json& override)
{
	try {
		if (!override.is_object()) {
			logger::info("Attempting to merge non-object JSON data, skipping");
			return;
		}

		for (const auto& [key, value] : override.items()) {
			// Skip metadata fields during merge
			if (key.length() > 0 && key[0] == '_') {
				continue;
			}

			// Validate key length
			if (key.length() > MAX_STRING_LENGTH) {
				logger::info("Skipping merge of key '{}' - too long", key.substr(0, 50) + "...");
				continue;
			}

			try {
				if (target.contains(key) && target[key].is_object() && value.is_object()) {
					// Recursively merge objects
					MergeJson(target[key], value);
				} else {
					// Validate the value before assignment
					if (value.is_string()) {
						std::string strValue = value.get<std::string>();
						if (strValue.length() > MAX_STRING_LENGTH) {
							logger::info("Truncating string value for key '{}' - too long", key);
							target[key] = strValue.substr(0, MAX_STRING_LENGTH);
						} else {
							target[key] = value;
						}
					} else if (value.is_number()) {
						double numValue = value.get<double>();
						if (std::isnan(numValue) || std::isinf(numValue)) {
							logger::info("Skipping merge of key '{}' - invalid numeric value", key);
							continue;
						}
						if (numValue > MAX_NUMERIC_VALUE || numValue < MIN_NUMERIC_VALUE) {
							logger::info("Clamping numeric value for key '{}'", key);
							target[key] = std::clamp(numValue, MIN_NUMERIC_VALUE, MAX_NUMERIC_VALUE);
						} else {
							target[key] = value;
						}
					} else if (value.is_array()) {
						if (value.size() > MAX_ARRAY_SIZE) {
							logger::info("Truncating array for key '{}' - too large", key);
							json truncatedArray = json::array();
							for (size_t i = 0; i < std::min(value.size(), MAX_ARRAY_SIZE); ++i) {
								truncatedArray.push_back(value[i]);
							}
							target[key] = truncatedArray;
						} else {
							target[key] = value;
						}
					} else if (value.is_object()) {
						if (value.size() > MAX_OBJECT_SIZE) {
							logger::info("Skipping merge of key '{}' - object too large", key);
							continue;
						}
						target[key] = value;
					} else {
						// Boolean, null, or other safe types
						target[key] = value;
					}
				}
			} catch (const json::exception& e) {
				logger::info("JSON error merging key '{}': {}", key, e.what());
				continue;
			} catch (const std::exception& e) {
				logger::info("Error merging key '{}': {}", key, e.what());
				continue;
			}
		}
	} catch (const json::exception& e) {
		logger::info("JSON error during merge operation: {}", e.what());
	} catch (const std::exception& e) {
		logger::info("Error during merge operation: {}", e.what());
	}
}

void SettingsOverrideManager::ReportOverrideFailure(const std::string& modName, const std::string& featureName, const std::string& errorMessage)
{
	// Create a feature file info for the override failure
	FeatureIssues::FeatureFileInfo fileInfo;
	fileInfo.featureName = featureName.empty() ? "Global" : featureName;

	// Try to find the override file path for better error reporting
	std::string filename = modName + "_" + (featureName.empty() ? "Global" : featureName) + ".json";
	auto overridesDir = GetOverridesDirectory();
	auto filePath = overridesDir / filename;

	if (std::filesystem::exists(filePath)) {
		fileInfo.hasINI = true;  // Using hasINI to indicate file exists (even though it's JSON)
		fileInfo.iniPath = filePath.string();

		try {
			auto timestamp = std::filesystem::last_write_time(filePath);
			fileInfo.latestTimestamp = timestamp;
			fileInfo.latestTimestampFile = filePath.string();

			// Format timestamp for display
			auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				timestamp - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			auto time_t = std::chrono::system_clock::to_time_t(sctp);
			std::stringstream ss;
			ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
			fileInfo.timestampDisplay = ss.str();
		} catch (const std::exception&) {
			fileInfo.timestampDisplay = "Unknown";
		}
	}

	// Create a descriptive error message
	std::string fullErrorMessage;
	if (featureName.empty()) {
		fullErrorMessage = "Global override from mod '" + modName + "' failed to load: " + errorMessage;
	} else {
		fullErrorMessage = "Override for feature '" + featureName + "' from mod '" + modName + "' failed to load: " + errorMessage;
	}

	// Add to Feature Issues as an override failure
	FeatureIssues::AddFeatureIssue(
		modName + (featureName.empty() ? "_Global" : "_" + featureName),  // Use combined name as shortName
		"unknown",                                                        // version
		fullErrorMessage,
		FeatureIssues::FeatureIssueInfo::IssueType::OVERRIDE_FAILED,
		fileInfo);
}
