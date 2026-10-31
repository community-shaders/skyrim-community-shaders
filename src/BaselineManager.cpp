#include "BaselineManager.h"

#include "Feature.h"
#include "FeatureIssues.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "Util.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsCatalog.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

using namespace SKSE;

namespace
{
	constexpr size_t MAX_BASELINE_FILE_SIZE = 1024 * 1024;
	constexpr int kBaselineJsonIndent = 2;

	/** @brief Parses a Baseline file without the discovery-time filename and sanitization passes. */
	std::optional<json> ReadBaselineDocument(const std::filesystem::path& path)
	{
		return Util::FileHelpers::ReadJsonObject(path, "Baseline file", MAX_BASELINE_FILE_SIZE);
	}

	/** @brief Reports, once per session, files left where older builds read overrides: nothing reads them now. */
	void ReportLegacyOverrides()
	{
		static bool reported = false;
		if (std::exchange(reported, true))
			return;
		size_t count = 0;
		try {
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(Util::PathHelpers::GetCommunityShaderPath() / "Overrides", ec))
				count += entry.path().extension() == ".json" ? 1 : 0;
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Could not scan the legacy Overrides folder: {}", e.what());
		}
		if (count == 0)
			return;

		const auto message = std::format(
			"{} old-format override file(s) in CommunityShaders/Overrides are not compatible anymore and are no longer read. "
			"Update them to the new Baseline format: move feature files into the Baseline folder of a preset pack "
			"(Presets/<Name>/Baseline/) and enable that pack on the Presets page. "
			"Global files (*_Global.json) cannot be moved, as Baseline packs only hold feature settings; "
			"set those values again in the menu.",
			count);
		logger::warn("{}", message);
		FeatureIssues::FeatureFileInfo fileInfo;
		fileInfo.featureName = "Overrides";
		FeatureIssues::AddFeatureIssue("Overrides", "unknown", message, FeatureIssues::FeatureIssueInfo::IssueType::BASELINE_FAILED, fileInfo);
	}

	/** @brief Erases one setting leaf, then any groups it leaves empty. */
	void EraseSettingPath(json& document, json::json_pointer pointer)
	{
		while (!pointer.empty() && document.contains(pointer)) {
			auto& parent = document.at(pointer.parent_pointer());
			parent.erase(pointer.back());
			if (!parent.empty())
				return;
			pointer = pointer.parent_pointer();
		}
	}
}

size_t BaselineManager::DiscoverFiles()
{
	ReportLegacyOverrides();
	files.clear();
	featureFileMap.clear();
	++revision;

	// Each enabled pack in layering order: later files win merge conflicts.
	for (const auto& packId : UnifiedPresetCatalog::GetSingleton().GetBaselinePackIds()) {
		for (const auto& packPath : Util::PathHelpers::GetUnifiedPackPaths(packId))
			DiscoverDirectory(packPath / UnifiedPresetCatalog::kBaselineSubdir, packId);
	}

	discovered = true;
	logger::info("Discovered {} Baseline files", files.size());
	return files.size();
}

std::string BaselineManager::ParseBaselineFeatureName(const std::filesystem::path& filePath)
{
	const auto filename = Util::PathHelpers::ToUtf8(filePath.filename());
	if (filePath.extension() != ".json" || filename.empty() || filename[0] == '.' || filename[0] == '~') {
		return {};
	}

	auto name = Util::PathHelpers::ToUtf8(filePath.stem());
	if (const auto underscore = name.find_last_of('_'); underscore != std::string::npos) {
		name.erase(0, underscore + 1);
	}
	return name;
}

size_t BaselineManager::DiscoverDirectory(const std::filesystem::path& directory, const std::string& packId)
{
	if (!std::filesystem::exists(directory)) {
		logger::info("Baseline directory does not exist: {}", Util::PathHelpers::ToUtf8(directory));
		return 0;
	}

	logger::info("Discovering Baseline files in: {}", Util::PathHelpers::ToUtf8(directory));

	size_t filesProcessed = 0;
	size_t filesLoaded = 0;
	size_t maxFilesToProcess = 1000;  // Prevent processing too many files

	try {
		// Sorted by name so a pack's files layer in the same order on every machine; a later file wins conflicts.
		std::vector<std::filesystem::directory_entry> entries;
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			// Safety check to prevent infinite loops or DoS
			if (entries.size() >= maxFilesToProcess) {
				logger::info("Reached maximum file processing limit ({}), stopping discovery", maxFilesToProcess);
				break;
			}
			entries.push_back(entry);
		}
		std::ranges::sort(entries, {}, [](const auto& entry) { return entry.path().filename(); });

		for (const auto& entry : entries) {
			filesProcessed++;

			// Skip if not a regular file or not a JSON file
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}

			// Check filename length to prevent overly long names
			std::string filename = Util::PathHelpers::ToUtf8(entry.path().filename());
			if (filename.length() > MAX_STRING_LENGTH) {
				logger::info("Skipping Baseline file with overly long name: {}", filename.substr(0, 50) + "...");
				continue;
			}

			// Skip hidden files or files starting with special characters
			if (filename.empty() || filename[0] == '.' || filename[0] == '~') {
				logger::info("Skipping hidden/temporary file: {}", filename);
				continue;
			}

			// Pack files are per-feature only; this is what keeps Advanced/General/Menu keys (e.g. compiler threads) unshippable.
			// Checked against the build rather than load state: discovery runs before features load at boot.
			const auto featureName = ParseBaselineFeatureName(entry.path());
			if (!Feature::IsFeatureKnown(featureName)) {
				logger::warn("Baseline file {} targets '{}', which is not a feature in this build; skipping", Util::PathHelpers::ToUtf8(entry.path()), featureName);
				ReportFailure(packId, featureName, "Feature is not part of this Community Shaders build", entry.path());
				continue;
			}

			try {
				auto baselineFile = LoadFile(entry.path(), packId);
				if (baselineFile && !baselineFile->enabled) {
					logger::info("Skipping Baseline file {}: its _metadata disables it", Util::PathHelpers::ToUtf8(entry.path()));
				} else if (baselineFile) {
					size_t index = files.size();
					files.push_back(std::move(*baselineFile));

					// Map files by feature for quick lookup
					const auto& file = files[index];
					featureFileMap[file.featureName].push_back(index);

					filesLoaded++;
					logger::info("Loaded Baseline file: {} for {}", file.packId, file.featureName);
				} else {
					ReportFailure(packId, featureName, "File could not be loaded or parsed", entry.path());
				}
			} catch (const std::exception& e) {
				logger::info("Error loading Baseline file {}: {}", Util::PathHelpers::ToUtf8(entry.path()), e.what());
				ReportFailure(packId, featureName, e.what(), entry.path());
				continue;
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::info("Error accessing Baseline directory: {}", e.what());
	} catch (const std::exception& e) {
		logger::info("Unexpected error during Baseline discovery: {}", e.what());
	}

	logger::info("Loaded {} Baseline files from {} ({} processed)", filesLoaded, Util::PathHelpers::ToUtf8(directory), filesProcessed);
	return filesProcessed;
}

size_t BaselineManager::ApplyBaselines(const std::string& featureName, json& featureJson)
{
	if (!discovered) {
		return 0;
	}

	size_t appliedCount = 0;

	auto it = featureFileMap.find(featureName);
	if (it != featureFileMap.end()) {
		for (size_t index : it->second) {
			auto& file = files[index];
			if (!RejectsUnknownKeys(file, featureJson)) {
				try {
					Util::Settings::MergeSettings(featureJson, file.values);
					appliedCount++;
				} catch (const std::exception& e) {
					logger::info("Failed to apply Baseline file from {} to {}: {}", file.packId, featureName, e.what());
					ReportFailure(file.packId, featureName, "Failed to apply: " + std::string(e.what()), Util::PathHelpers::FromUtf8(file.filePath));
				}
			}
		}
	}

	return appliedCount;
}

std::vector<const BaselineManager::BaselineFile*> BaselineManager::GetFeatureFiles(const std::string& featureName) const
{
	std::vector<const BaselineFile*> result;

	auto it = featureFileMap.find(featureName);
	if (it != featureFileMap.end()) {
		for (size_t index : it->second) {
			result.push_back(&files[index]);
		}
	}

	return result;
}

bool BaselineManager::HasFeatureFiles(const std::string& featureName) const
{
	if (!discovered) {
		return false;
	}

	auto it = featureFileMap.find(featureName);
	return it != featureFileMap.end() && !it->second.empty();
}

void BaselineManager::Refresh()
{
	discovered = false;
	DiscoverFiles();
}

std::unique_ptr<BaselineManager::BaselineFile> BaselineManager::LoadFile(const std::filesystem::path& filePath, const std::string& packId)
{
	const auto fileJson = ReadBaselineDocument(filePath);
	if (!fileJson || !IsValidDocument(*fileJson, filePath))
		return nullptr;

	auto baselineFile = std::make_unique<BaselineFile>();
	baselineFile->packId = packId;
	baselineFile->featureName = ParseBaselineFeatureName(filePath);
	assert(Feature::IsFeatureKnown(baselineFile->featureName));
	baselineFile->filePath = Util::PathHelpers::ToUtf8(filePath);

	// ValidateFormat has checked the metadata fields' types and lengths.
	if (const auto metadata = fileJson->find("_metadata"); metadata != fileJson->end()) {
		baselineFile->version = metadata->value("version", "");
		baselineFile->description = metadata->value("description", "");
		baselineFile->enabled = metadata->value("enabled", true);
	}

	baselineFile->values = SanitizeJsonData(*fileJson);
	Util::Settings::EraseMetadataKeys(baselineFile->values);
	return baselineFile;
}

bool BaselineManager::ValidateFormat(const json& fileJson, const std::string& filePath)
{
	try {
		// Basic validation - must be an object
		if (!fileJson.is_object()) {
			if (!filePath.empty()) {
				logger::info("Baseline file must contain a JSON object: {}", filePath);
			}
			return false;
		}

		// Check object size limits
		if (fileJson.size() > MAX_OBJECT_SIZE) {
			if (!filePath.empty()) {
				logger::info("Baseline file contains too many top-level properties (max {}): {}", MAX_OBJECT_SIZE, filePath);
			}
			return false;
		}

		// Must have at least one non-metadata field
		bool hasNonMetadata = false;
		for (const auto& [key, value] : fileJson.items()) {
			if (key.length() == 0 || key[0] != '_') {
				hasNonMetadata = true;
				break;
			}
		}

		if (!hasNonMetadata) {
			if (!filePath.empty()) {
				logger::info("Baseline file contains only metadata, no settings: {}", filePath);
			}
			return false;
		}

		// Validate metadata section if present
		if (fileJson.contains("_metadata")) {
			const auto& metadata = fileJson["_metadata"];
			if (!metadata.is_object()) {
				if (!filePath.empty()) {
					logger::info("_metadata must be an object in Baseline file: {}", filePath);
				}
				return false;
			}

			// Validate metadata fields
			for (const auto& [key, value] : metadata.items()) {
				if (key == "version" || key == "description") {
					if (!value.is_string()) {
						if (!filePath.empty()) {
							logger::info("Metadata field '{}' must be a string in Baseline file: {}", key, filePath);
						}
						return false;
					}
					if (value.get<std::string>().length() > MAX_STRING_LENGTH) {
						if (!filePath.empty()) {
							logger::info("Metadata field '{}' too long (max {} chars) in Baseline file: {}", key, MAX_STRING_LENGTH, filePath);
						}
						return false;
					}
				} else if (key == "enabled") {
					if (!value.is_boolean()) {
						if (!filePath.empty()) {
							logger::info("Metadata field 'enabled' must be a boolean in Baseline file: {}", filePath);
						}
						return false;
					}
				} else if (key == "modName") {
					// Allow modName in metadata but validate it
					if (!value.is_string()) {
						if (!filePath.empty()) {
							logger::info("Metadata field 'modName' must be a string in Baseline file: {}", filePath);
						}
						return false;
					}
				} else {
					// Unknown metadata field - warn but don't fail
					if (!filePath.empty()) {
						logger::info("Unknown metadata field '{}' in Baseline file: {}", key, filePath);
					}
				}
			}
		}

		return true;

	} catch (const json::exception& e) {
		if (!filePath.empty()) {
			logger::info("JSON error during validation of Baseline file {}: {}", filePath, e.what());
		}
		return false;
	} catch (const std::exception& e) {
		if (!filePath.empty()) {
			logger::info("Error during validation of Baseline file {}: {}", filePath, e.what());
		}
		return false;
	}
}

bool BaselineManager::ValidateJsonDataTypes(const json& jsonData, const std::string& path, const std::string& filePath, size_t depth)
{
	try {
		// Counted rather than read off the path, which arrays extend without a dot: this recursion is the stack.
		if (depth > MAX_JSON_DEPTH) {
			if (!filePath.empty()) {
				logger::info("JSON nesting too deep at '{}' (max {}) in Baseline file: {}", path, MAX_JSON_DEPTH, filePath);
			}
			return false;
		}

		if (jsonData.is_object()) {
			if (jsonData.size() > MAX_OBJECT_SIZE) {
				if (!filePath.empty()) {
					logger::info("JSON object too large at '{}' (max {} properties) in Baseline file: {}", path, MAX_OBJECT_SIZE, filePath);
				}
				return false;
			}

			for (const auto& [key, value] : jsonData.items()) {
				// Validate key length
				if (key.length() > MAX_STRING_LENGTH) {
					if (!filePath.empty()) {
						logger::info("JSON property name too long at '{}' (max {} chars) in Baseline file: {}", path + "." + key, MAX_STRING_LENGTH, filePath);
					}
					return false;
				}

				// Recursively validate value
				std::string newPath = path.empty() ? key : path + "." + key;
				if (!ValidateJsonDataTypes(value, newPath, filePath, depth + 1)) {
					return false;
				}
			}
		} else if (jsonData.is_array()) {
			if (jsonData.size() > MAX_ARRAY_SIZE) {
				if (!filePath.empty()) {
					logger::info("JSON array too large at '{}' (max {} elements) in Baseline file: {}", path, MAX_ARRAY_SIZE, filePath);
				}
				return false;
			}

			for (size_t i = 0; i < jsonData.size(); ++i) {
				std::string newPath = path + "[" + std::to_string(i) + "]";
				if (!ValidateJsonDataTypes(jsonData[i], newPath, filePath, depth + 1)) {
					return false;
				}
			}
		} else if (jsonData.is_string()) {
			if (jsonData.get<std::string>().length() > MAX_STRING_LENGTH) {
				if (!filePath.empty()) {
					logger::info("JSON string too long at '{}' (max {} chars) in Baseline file: {}", path, MAX_STRING_LENGTH, filePath);
				}
				return false;
			}
		} else if (jsonData.is_number()) {
			double value = jsonData.get<double>();
			if (value > MAX_NUMERIC_VALUE || value < MIN_NUMERIC_VALUE) {
				if (!filePath.empty()) {
					logger::info("JSON numeric value out of range at '{}' ({} to {}) in Baseline file: {}", path, MIN_NUMERIC_VALUE, MAX_NUMERIC_VALUE, filePath);
				}
				return false;
			}

			// Check for NaN and infinity
			if (std::isnan(value) || std::isinf(value)) {
				if (!filePath.empty()) {
					logger::info("JSON numeric value is NaN or infinity at '{}' in Baseline file: {}", path, filePath);
				}
				return false;
			}
		}
		// Boolean and null values are always valid

		return true;

	} catch (const json::exception& e) {
		if (!filePath.empty()) {
			logger::info("JSON error during data type validation at '{}' in Baseline file {}: {}", path, filePath, e.what());
		}
		return false;
	} catch (const std::exception& e) {
		if (!filePath.empty()) {
			logger::info("Error during data type validation at '{}' in Baseline file {}: {}", path, filePath, e.what());
		}
		return false;
	}
}

json BaselineManager::SanitizeJsonData(const json& jsonData)
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
						logger::info("Removing potentially dangerous key from file: {}", key);
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
				// Lengths and numeric ranges were checked by ValidateJsonDataTypes; only control characters remain to strip.
				auto& str = data.get_ref<std::string&>();
				std::erase_if(str, [](char c) { return c >= 0 && c < 32 && c != '\t' && c != '\n' && c != '\r'; });
			}
		};

		sanitizeRecursive(sanitized);
		return sanitized;

	} catch (const std::exception& e) {
		logger::info("Error during JSON sanitization: {}", e.what());
		return json::object();  // Return empty object on error
	}
}

bool BaselineManager::RejectsUnknownKeys(BaselineFile& file, const json& featureJson)
{
	// The merge would carry unknown keys through silently; an empty target has no shape, so keep the last verdict.
	if (featureJson.is_object() && !featureJson.empty()) {
		const bool alreadyReported = !file.unknownKeys.empty();
		file.unknownKeys.clear();
		Util::Settings::CollectUnknownSettingKeys(file.values, featureJson, "", file.unknownKeys);
		if (!file.unknownKeys.empty() && !alreadyReported) {
			std::string keys;
			for (const auto& key : file.unknownKeys)
				keys += (keys.empty() ? "" : ", ") + key;
			logger::warn("Skipped Baseline file from {} to {}: unrecognized keys {}", file.packId, file.featureName, keys);
			ReportFailure(file.packId, file.featureName, "Unrecognized settings keys: " + keys, Util::PathHelpers::FromUtf8(file.filePath));
		}
	}
	return !file.unknownKeys.empty();
}

void BaselineManager::ReportFailure(const std::string& packId, const std::string& featureName, const std::string& errorMessage,
	const std::filesystem::path& filePath)
{
	FeatureIssues::FeatureFileInfo fileInfo;
	fileInfo.featureName = featureName;

	std::error_code ec;
	if (!filePath.empty() && std::filesystem::exists(filePath, ec)) {
		fileInfo.hasINI = true;  // Using hasINI to indicate file exists (even though it's JSON)
		fileInfo.iniPath = Util::PathHelpers::ToUtf8(filePath);

		fileInfo.latestTimestamp = std::filesystem::last_write_time(filePath, ec);
		fileInfo.latestTimestampFile = Util::PathHelpers::ToUtf8(filePath);
		fileInfo.timestampDisplay = ec ? "Unknown" : FeatureIssues::FormatFileTimestamp(fileInfo.latestTimestamp);
	}

	const auto fullErrorMessage = "Baseline file for feature '" + featureName + "' from preset pack '" + packId + "' failed to load: " + errorMessage;

	FeatureIssues::AddFeatureIssue(
		packId + "_" + featureName,  // Combined name as shortName
		"unknown",                   // version
		fullErrorMessage,
		FeatureIssues::FeatureIssueInfo::IssueType::BASELINE_FAILED,
		fileInfo);
}

bool BaselineManager::IsApplicable(const BaselineFile& info) const
{
	const auto* feature = Feature::FindFeatureByShortName(info.featureName);
	return feature && feature->UsesMainSettings();
}

bool BaselineManager::IsValidDocument(const json& document, const std::filesystem::path& filePath)
{
	const auto path = Util::PathHelpers::ToUtf8(filePath);
	return ValidateFormat(document, path) && ValidateJsonDataTypes(document, "", path);
}

std::filesystem::path BaselineManager::GetExportDestination(const std::string& packName, const std::string& featureName) const
{
	const auto safeName = Util::FileHelpers::SanitizeFileName(packName);
	if (safeName.empty()) {
		return {};
	}
	// Resolves through the Data (VFS) path so new files land in the MO2 overwrite folder.
	return Util::PathHelpers::GetUnifiedPackPath(safeName) / UnifiedPresetCatalog::kBaselineSubdir / std::format("{}.json", featureName);
}

std::optional<json> BaselineManager::ReadExportTarget(const std::filesystem::path& destination)
{
	std::error_code ec;
	if (!std::filesystem::exists(destination, ec)) {
		return json::object();
	}
	return ReadBaselineDocument(destination);
}

bool BaselineManager::ExportSettings(const std::string& packName, const std::string& featureName,
	std::span<const std::string> settingPaths, const json& featureSettings, std::span<const std::string> removePaths)
{
	const auto destination = GetExportDestination(packName, featureName);
	auto* feature = Feature::FindFeatureByShortName(featureName);
	if (destination.empty() || (settingPaths.empty() && removePaths.empty()) || !feature || !feature->UsesMainSettings()) {
		return false;
	}

	const auto available = Util::Settings::GetExportSettings(featureName, featureSettings);
	for (const auto& paths : { settingPaths, removePaths }) {
		for (const auto& path : paths) {
			if (std::ranges::find(available, path, &Util::Settings::ExportSetting::path) == available.end()) {
				logger::error("Cannot export unknown setting '{}' of {}", path, featureName);
				return false;
			}
		}
	}

	const auto selected = Util::Settings::SelectSettingPaths(featureSettings, { settingPaths.begin(), settingPaths.end() });
	if (selected.empty() && !settingPaths.empty()) {
		return false;
	}

	// Merge into an existing file of the same name so repeated exports accumulate rather than truncate,
	// reading it raw so any _metadata the author wrote survives.
	auto document = ReadExportTarget(destination);
	if (!document) {
		logger::error("Refusing to overwrite unreadable Baseline file {}", Util::PathHelpers::ToUtf8(destination));
		return false;
	}
	for (const auto& path : removePaths) {
		EraseSettingPath(*document, json::json_pointer(path));
	}
	document->update(selected, true);

	if (!IsValidDocument(*document, destination)) {
		return false;
	}

	const auto packRoot = destination.parent_path().parent_path();
	Util::FileHelpers::EnsureDirectoryExists(destination.parent_path());
	if (!UnifiedPresetCatalog::EnsureBaselineManifest(packRoot, packName)) {
		return false;
	}

	// Atomic so a failed export cannot leave a pack's Baseline file truncated.
	if (!Util::FileHelpers::WriteJsonAtomically(destination, *document, kBaselineJsonIndent, "Baseline file")) {
		return false;
	}

	logger::info("Exported {} setting(s) of {} to {}, removed {}", settingPaths.size(), featureName, Util::PathHelpers::ToUtf8(destination), removePaths.size());
	return true;
}
