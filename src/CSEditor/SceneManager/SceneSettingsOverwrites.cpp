#include "SceneSettingsOverwrites.h"

#include "Feature.h"
#include "Menu/Fonts.h"

#include <algorithm>
#include <filesystem>

using namespace SceneSettingsInternal;

namespace SceneSettingsOverwrites
{
	bool HasOverwriteEntryForPeriod(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		const SceneSettingsManager::SettingEntry& candidate)
	{
		return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
			return entry.source == SceneSettingsManager::EntrySource::Overwrite &&
			       entry.period == candidate.period &&
			       IsSameSetting(entry, candidate.featureShortName, candidate.settingPath, candidate.settingKey);
		});
	}

	bool AddOverwriteEntryIfUnique(std::vector<SceneSettingsManager::SettingEntry>& entries,
		SceneSettingsManager::SettingEntry&& entry, std::string_view context)
	{
		// Files are scanned lexicographically. The first overwrite for an address and period wins.
		if (HasOverwriteEntryForPeriod(entries, entry)) {
			logger::warn("[SceneSettings] Duplicate {} overwrite for {}.{} ({}) skipped",
				context, entry.featureShortName, entry.settingKey, entry.sourceFilename);
			return false;
		}

		entries.push_back(std::move(entry));
		return true;
	}

	std::filesystem::path GetOverwriteDir(const std::filesystem::path& baseDir,
		SceneSettingsManager::TimeOfDayPeriod period)
	{
		return period != SceneSettingsManager::TimeOfDayPeriod::Count ?
		           baseDir / SceneSettingsManager::GetPeriodName(period) :
		           baseDir;
	}

	std::filesystem::path GetOverwriteFilePath(const std::filesystem::path& baseDir,
		const SceneSettingsManager::SettingEntry& entry)
	{
		if (!entry.sourcePath.empty())
			return entry.sourcePath;
		return GetOverwriteDir(baseDir, entry.period) / entry.sourceFilename;
	}

	std::string GetOverwriteTypeDescription(std::string_view sceneLabel,
		SceneSettingsManager::TimeOfDayPeriod period)
	{
		return period != SceneSettingsManager::TimeOfDayPeriod::Count ?
		           std::format("{} - {}", sceneLabel, SceneSettingsManager::GetPeriodName(period)) :
		           std::string(sceneLabel);
	}

	std::filesystem::path GetSceneOverwritePath(SceneSettingsManager::SceneType type,
		const SceneSettingsManager::SettingEntry& entry)
	{
		return GetOverwriteFilePath(SceneSettingsManager::GetOverwritesPath(type), entry);
	}

	std::filesystem::path GetWeatherOverwritePath(RE::FormID weatherId, const SceneSettingsManager::SettingEntry& entry)
	{
		return GetOverwriteFilePath(
			SceneSettingsManager::GetWeatherOverwritesDir() / Util::FormIdToSpid(weatherId), entry);
	}

	std::filesystem::path GetLocationOverwritePath(std::string_view formKey,
		const SceneSettingsManager::SettingEntry& entry)
	{
		return GetOverwriteFilePath(SceneSettingsManager::GetLocationOverwritesDir() / formKey, entry);
	}

	bool WriteGroupedOverwriteFile(const std::filesystem::path& allowedRoot, const std::filesystem::path& path,
		const std::string& featureShortName, const std::string& overwriteType,
		const std::vector<const SceneSettingsManager::SettingEntry*>& entries, const json& extraMetadata)
	{
		if (path.lexically_normal() == allowedRoot.lexically_normal() || !Util::IsPathWithinDirectory(allowedRoot, path)) {
			logger::error("[SceneSettings] Refusing to write overwrite outside '{}': {}", allowedRoot.string(), path.string());
			return false;
		}

		std::error_code ec;
		const auto pathExists = std::filesystem::exists(path, ec);
		if (ec) {
			logger::error("[SceneSettings] WriteGroupedOverwriteFile: could not inspect '{}': {}", path.string(), ec.message());
			return false;
		}

		json data = json::object();
		if (pathExists && !ReadBoundedSceneJson(path, data)) {
			logger::error("[SceneSettings] Refusing to replace invalid overwrite file '{}'", path.string());
			return false;
		}

		if (auto featureIt = data.find(kFeatureKey); featureIt != data.end() &&
			(!featureIt->is_string() || featureIt->get<std::string>() != featureShortName)) {
			logger::error("[SceneSettings] Refusing to relabel overwrite file '{}' from another feature", path.string());
			return false;
		}
		data[kFeatureKey] = featureShortName;
		auto& metadata = data[kMetadataKey];
		if (!metadata.is_null() && !metadata.is_object()) {
			logger::error("[SceneSettings] Refusing to replace invalid metadata in overwrite file '{}'", path.string());
			return false;
		}
		if (metadata.is_null())
			metadata = json::object();
		metadata[kMetadataDescriptionKey] = std::format("{} scene settings overwrite ({})",
			SceneSettingsManager::GetFeatureDisplayName(featureShortName), overwriteType);
		if (extraMetadata.is_object())
			for (const auto& [key, value] : extraMetadata.items())
				metadata[key] = value;
		auto& entryTransitions = metadata[kMetadataEntryTransitionsKey];
		if (!entryTransitions.is_null() && !entryTransitions.is_object()) {
			logger::error("[SceneSettings] Refusing to replace invalid entry transition metadata in overwrite file '{}'",
				path.string());
			return false;
		}
		if (entryTransitions.is_null())
			entryTransitions = json::object();
		for (const auto* entry : entries) {
			auto* node = GetObjectAtPath(data, entry->settingPath, true);
			if (!node) {
				logger::error("[SceneSettings] Refusing to replace a non-object path in overwrite file '{}'",
					path.string());
				return false;
			}
			(*node)[entry->settingKey] = entry->value;
			if (!entry->transitionSeconds) {
				RemoveObjectValueAtPath(entryTransitions, entry->settingPath, 0, entry->settingKey);
				continue;
			}
			auto* transitionNode = GetObjectAtPath(entryTransitions, entry->settingPath, true);
			if (!transitionNode) {
				logger::error("[SceneSettings] Refusing to replace a non-object transition path in overwrite file '{}'",
					path.string());
				return false;
			}
			(*transitionNode)[entry->settingKey] = *entry->transitionSeconds;
		}
		if (entryTransitions.empty())
			metadata.erase(kMetadataEntryTransitionsKey);

		return WriteJsonAtomically(path, data, kOverwriteJsonIndent, "overwrite file");
	}

	bool RemoveSettingFromOverwriteFile(const std::filesystem::path& path,
		const std::vector<std::string>& settingPath, const std::string& settingKey)
	{
		if (path.empty())
			return true;

		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return !ec;

		// The read handle has to be closed before the rewrite below: Windows refuses to replace or
		// delete a file that still has one open.
		json data;
		if (!ReadBoundedSceneJson(path, data)) {
			logger::error("[SceneSettings] Could not read overwrite file '{}' for editing", path.string());
			return false;
		}

		if (!RemoveObjectValueAtPath(data, settingPath, 0, settingKey)) {
			logger::error("[SceneSettings] Overwrite setting '{}' was not found in '{}'",
				settingKey, path.string());
			return false;
		}
		if (auto metadataIt = data.find(kMetadataKey); metadataIt != data.end() && metadataIt->is_object()) {
			if (auto transitionsIt = metadataIt->find(kMetadataEntryTransitionsKey);
				transitionsIt != metadataIt->end() && transitionsIt->is_object()) {
				RemoveObjectValueAtPath(*transitionsIt, settingPath, 0, settingKey);
				if (transitionsIt->empty())
					metadataIt->erase(transitionsIt);
			}
		}
		if (!HasSceneOverwriteContent(data)) {
			auto removed = std::filesystem::remove(path, ec);
			if (removed || !ec)
				return true;
			logger::error("[SceneSettings] Failed to delete overwrite file '{}': {}", path.string(), ec.message());
			return false;
		}

		return WriteJsonAtomically(path, data, kOverwriteJsonIndent, "overwrite file");
	}

	bool ParseOverwriteFileEntries(const std::filesystem::path& filePath,
		SceneSettingsManager::SceneType allowedType, bool requireNumeric,
		std::vector<SceneSettingsManager::SettingEntry>& outEntries, FeatureSettingsCache* featureSettingsCache,
		std::optional<bool>* timeOfDayEnabled)
	{
		json data;
		if (!ReadBoundedSceneJson(filePath, data)) {
			logger::warn("[SceneSettings] Overwrite '{}' is invalid or exceeds {} bytes", filePath.string(),
				kMaxSceneOverwriteFileSize);
			return false;
		}
		return ParseOverwriteFileEntries(data, filePath, allowedType, requireNumeric, outEntries,
			featureSettingsCache, timeOfDayEnabled);
	}

	bool ParseOverwriteFileEntries(const json& data, const std::filesystem::path& filePath,
		SceneSettingsManager::SceneType allowedType, bool requireNumeric,
		std::vector<SceneSettingsManager::SettingEntry>& outEntries, FeatureSettingsCache* featureSettingsCache,
		std::optional<bool>* timeOfDayEnabled)
	{
		using SSM = SceneSettingsManager;

		const json* entryTransitions = nullptr;
		if (auto metadataIt = data.find(kMetadataKey); metadataIt != data.end() && metadataIt->is_object()) {
			if (auto modeIt = metadataIt->find(kTimeOfDayEnabledKey); timeOfDayEnabled && modeIt != metadataIt->end()) {
				if (modeIt->is_boolean())
					*timeOfDayEnabled = modeIt->get<bool>();
				else
					logger::warn("[SceneSettings] Overwrite '{}' {} metadata must be boolean", filePath.string(),
						kTimeOfDayEnabledKey);
			}
			// Only the location layer transitions, so any other scene ignores the field.
			if (auto transitionsIt = metadataIt->find(kMetadataEntryTransitionsKey);
				allowedType == SSM::SceneType::Location && transitionsIt != metadataIt->end()) {
				if (transitionsIt->is_object())
					entryTransitions = &*transitionsIt;
				else
					logger::warn("[SceneSettings] Overwrite '{}' has invalid {} metadata", filePath.string(),
						kMetadataEntryTransitionsKey);
			}
		}

		std::string featureShortName = data.value(kFeatureKey, "");
		if (featureShortName.empty()) {
			auto stem = filePath.stem().string();
			auto lastUnderscore = stem.rfind('_');
			if (lastUnderscore != std::string::npos)
				featureShortName = stem.substr(lastUnderscore + 1);
		}

		if (!Feature::FindFeatureByShortName(featureShortName)) {
			logger::warn("[SceneSettings] Overwrite '{}' targets feature '{}', which is not loaded - skipping",
				filePath.string(), featureShortName);
			return false;
		}
		if (!SSM::IsFeatureAllowedForType(allowedType, featureShortName)) {
			logger::warn("[SceneSettings] Overwrite '{}' feature '{}' is not allowed for {} - skipping",
				filePath.string(), featureShortName, SSM::GetSceneTypeName(allowedType));
			return false;
		}

		bool foundAny = false;
		CollectOverwriteEntries(data, {}, [&](const auto& settingPath, const auto& key, const auto& value) {
			json parsedValue = value;
			WidenParsedIntegerToFloat(featureShortName, settingPath, key, parsedValue);
			if (!ValidateSceneSettingEntry("Overwrite", allowedType, featureShortName, settingPath, key, parsedValue,
					requireNumeric, featureSettingsCache))
				return;

			SSM::SettingEntry entry;
			if (const auto* transitionNode = entryTransitions ? GetObjectAtPath(*entryTransitions, settingPath) : nullptr)
				if (auto transitionIt = transitionNode->find(key); transitionIt != transitionNode->end()) {
					const auto seconds = transitionIt->is_number() ? std::optional{ transitionIt->get<float>() } : std::nullopt;
					if (seconds && std::isfinite(*seconds) && *seconds >= 0.0f && *seconds <= SSM::kMaxLocationTransitionSeconds)
						entry.transitionSeconds = seconds;
					else
						logger::warn("[SceneSettings] Overwrite '{}' transition for {} must be a number in 0..{}",
							filePath.string(), GetSettingLogName(featureShortName, settingPath, key),
							SSM::kMaxLocationTransitionSeconds);
				}
			entry.featureShortName = featureShortName;
			entry.settingPath = settingPath;
			entry.settingKey = key;
			entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, key);
			entry.value = std::move(parsedValue);
			entry.originalValue = entry.value;
			entry.source = SSM::EntrySource::Overwrite;
			entry.sourceFilename = filePath.filename().string();
			entry.sourcePath = filePath;
			outEntries.push_back(std::move(entry));
			foundAny = true;
		});
		return foundAny;
	}
}
