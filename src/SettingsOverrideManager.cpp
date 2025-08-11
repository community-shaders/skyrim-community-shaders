#include "SettingsOverrideManager.h"

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

	try {
		for (const auto& entry : std::filesystem::directory_iterator(overridesDir)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}

			auto overrideInfo = LoadOverrideFile(entry.path());
			if (overrideInfo) {
				size_t index = overrides.size();
				overrides.push_back(std::move(*overrideInfo));

				// Map feature overrides for quick lookup
				const auto& override = overrides[index];
				if (!override.isGlobal) {
					featureOverrideMap[override.featureName].push_back(index);
				}

				logger::info("Loaded override: {} for {}",
					override.modName,
					override.isGlobal ? "Global" : override.featureName);
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error accessing overrides directory: {}", e.what());
	}

	discovered = true;
	logger::info("Discovered {} override files", overrides.size());
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
					logger::debug("Applied override from {} to {}", override.modName, featureName);
				} catch (const std::exception& e) {
					logger::warn("Failed to apply override from {} to {}: {}",
						override.modName, featureName, e.what());
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
				logger::debug("Applied global override from {}", override.modName);
			} catch (const std::exception& e) {
				logger::warn("Failed to apply global override from {}: {}",
					override.modName, e.what());
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
	return std::filesystem::path(OVERRIDES_DIR);
}

json SettingsOverrideManager::LoadAppliedOverridesTracking() const
{
	json appliedOverrides;
	try {
		std::ifstream file(GetAppliedOverridesTrackingPath());
		if (file.is_open()) {
			file >> appliedOverrides;
		}
	} catch (const std::exception& e) {
		logger::debug("Failed to load applied overrides tracking: {}", e.what());
	}
	return appliedOverrides;
}

void SettingsOverrideManager::SaveAppliedOverridesTracking(const json& appliedOverrides) const
{
	try {
		std::filesystem::create_directories(std::filesystem::path(APPLIED_OVERRIDES_TRACKING_FILE).parent_path());
		std::ofstream file(GetAppliedOverridesTrackingPath());
		if (file.is_open()) {
			file << appliedOverrides.dump(1);
		}
	} catch (const std::exception& e) {
		logger::warn("Failed to save applied overrides tracking: {}", e.what());
	}
}

std::filesystem::path SettingsOverrideManager::GetAppliedOverridesTrackingPath() const
{
	return std::filesystem::path(APPLIED_OVERRIDES_TRACKING_FILE);
}

size_t SettingsOverrideManager::ApplyNewOverrides(json& baseSettings, json& appliedOverrides)
{
	if (!enabled || !discovered) {
		return 0;
	}

	size_t appliedCount = 0;
	auto currentTime = std::time(nullptr);

	for (const auto& override : overrides) {
		if (!override.enabled || !override.isGlobal) {
			continue;
		}

		// Create tracking key
		std::string trackingKey = override.modName + "_Global";

		// Check if this override has been applied before
		bool shouldApply = false;
		if (!appliedOverrides.contains(trackingKey)) {
			// First time seeing this override
			shouldApply = true;
		} else {
			// Check if the override file has changed
			auto& tracking = appliedOverrides[trackingKey];
			std::string currentHash = tracking.value("hash", "");
			if (currentHash != override.fileHash) {
				// Override file has changed, reapply
				shouldApply = true;
				logger::info("Override file {} has changed, reapplying", override.modName);
			}
		}

		if (shouldApply) {
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
			} catch (const std::exception& e) {
				logger::warn("Failed to apply global override from {}: {}", override.modName, e.what());
			}
		}
	}

	return appliedCount;
}

size_t SettingsOverrideManager::ApplyNewFeatureOverrides(const std::string& featureName, json& featureJson, json& appliedOverrides)
{
	if (!enabled || !discovered) {
		return 0;
	}

	size_t appliedCount = 0;
	auto currentTime = std::time(nullptr);

	auto it = featureOverrideMap.find(featureName);
	if (it != featureOverrideMap.end()) {
		for (size_t index : it->second) {
			const auto& override = overrides[index];
			if (!override.enabled) {
				continue;
			}

			// Create tracking key
			std::string trackingKey = override.modName + "_" + featureName;

			// Check if this override has been applied before
			bool shouldApply = false;
			if (!appliedOverrides.contains(trackingKey)) {
				// First time seeing this override
				shouldApply = true;
			} else {
				// Check if the override file has changed
				auto& tracking = appliedOverrides[trackingKey];
				std::string currentHash = tracking.value("hash", "");
				if (currentHash != override.fileHash) {
					// Override file has changed, reapply
					shouldApply = true;
					logger::info("Override file {} for {} has changed, reapplying", override.modName, featureName);
				}
			}

			if (shouldApply) {
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
				} catch (const std::exception& e) {
					logger::warn("Failed to apply override from {} to {}: {}", override.modName, featureName, e.what());
				}
			}
		}
	}

	return appliedCount;
}

std::unique_ptr<SettingsOverrideManager::OverrideInfo> SettingsOverrideManager::LoadOverrideFile(const std::filesystem::path& filePath)
{
	try {
		std::ifstream file(filePath);
		if (!file.is_open()) {
			logger::warn("Could not open override file: {}", filePath.string());
			return nullptr;
		}

		// Read entire file content for hash computation
		std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		// Parse JSON from content
		json overrideJson = json::parse(fileContent);

		if (!ValidateOverrideFormat(overrideJson)) {
			logger::warn("Invalid override file format: {}", filePath.string());
			return nullptr;
		}

		auto overrideInfo = std::make_unique<OverrideInfo>();

		auto [modName, featureName] = ParseOverrideFilename(filePath.filename().string());
		overrideInfo->modName = modName;
		overrideInfo->featureName = featureName;
		overrideInfo->filePath = filePath.string();
		overrideInfo->isGlobal = featureName.empty();
		overrideInfo->fileHash = ComputeContentHash(fileContent);

		// Extract metadata if present
		if (overrideJson.contains("_metadata") && overrideJson["_metadata"].is_object()) {
			const auto& metadata = overrideJson["_metadata"];
			if (metadata.contains("version") && metadata["version"].is_string()) {
				overrideInfo->version = metadata["version"];
			}
			if (metadata.contains("description") && metadata["description"].is_string()) {
				overrideInfo->description = metadata["description"];
			}
			if (metadata.contains("enabled") && metadata["enabled"].is_boolean()) {
				overrideInfo->enabled = metadata["enabled"];
			}
		}

		// Store the override data (excluding metadata)
		overrideInfo->overrideData = overrideJson;
		if (overrideInfo->overrideData.contains("_metadata")) {
			overrideInfo->overrideData.erase("_metadata");
		}

		return overrideInfo;

	} catch (const std::exception& e) {
		logger::warn("Error loading override file {}: {}", filePath.string(), e.what());
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

bool SettingsOverrideManager::ValidateOverrideFormat(const json& overrideJson)
{
	// Basic validation - must be an object
	if (!overrideJson.is_object()) {
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

	return hasNonMetadata;
}

void SettingsOverrideManager::MergeJson(json& target, const json& override)
{
	for (const auto& [key, value] : override.items()) {
		if (key.length() > 0 && key[0] == '_') {
			// Skip metadata fields during merge
			continue;
		}

		if (target.contains(key) && target[key].is_object() && value.is_object()) {
			// Recursively merge objects
			MergeJson(target[key], value);
		} else {
			// Direct assignment for primitives and arrays
			target[key] = value;
		}
	}
}
