#include "SceneSettingsOverwrites.h"

#include "Feature.h"
#include "Menu/Fonts.h"
#include "Utils/FileSystem.h"

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

	void MergeOverwriteFileEntries(SceneSettingsManager::PeriodicSceneConfig& config,
		std::vector<SceneSettingsManager::SettingEntry>&& entries, std::optional<bool> timeOfDayEnabled,
		SceneSettingsManager::TimeOfDayPeriod period, std::string_view context)
	{
		if (!config.overwriteTimeOfDayEnabled)
			config.overwriteTimeOfDayEnabled = timeOfDayEnabled;
		for (auto& entry : entries) {
			entry.period = period;
			AddOverwriteEntryIfUnique(config.entries, std::move(entry), context);
		}
	}

	std::filesystem::path GetOverwriteDir(const std::filesystem::path& baseDir,
		SceneSettingsManager::TimeOfDayPeriod period)
	{
		return period != SceneSettingsManager::TimeOfDayPeriod::Count ?
		           baseDir / SceneSettingsManager::GetPeriodName(period) :
		           baseDir;
	}

	std::string GetOverwriteTypeDescription(std::string_view sceneLabel,
		SceneSettingsManager::TimeOfDayPeriod period)
	{
		return period != SceneSettingsManager::TimeOfDayPeriod::Count ?
		           std::format("{} - {}", sceneLabel, SceneSettingsManager::GetPeriodName(period)) :
		           std::string(sceneLabel);
	}

	bool WriteGroupedOverwriteFile(const std::filesystem::path& allowedRoot, const std::filesystem::path& path,
		const std::string& featureShortName, const std::string& overwriteType,
		const std::vector<const SceneSettingsManager::SettingEntry*>& entries, const json& extraMetadata)
	{
		if (path.lexically_normal() == allowedRoot.lexically_normal() || !Util::IsPathWithinDirectory(allowedRoot, path)) {
			logger::error("[SceneSettings] Refusing to write overwrite outside '{}': {}", Util::PathHelpers::ToUtf8(allowedRoot), Util::PathHelpers::ToUtf8(path));
			return false;
		}

		std::error_code ec;
		const auto pathExists = std::filesystem::exists(path, ec);
		if (ec) {
			logger::error("[SceneSettings] WriteGroupedOverwriteFile: could not inspect '{}': {}", Util::PathHelpers::ToUtf8(path), ec.message());
			return false;
		}

		json data = json::object();
		if (pathExists && !ReadBoundedSceneJson(path, data)) {
			logger::error("[SceneSettings] Refusing to replace invalid overwrite file '{}'", Util::PathHelpers::ToUtf8(path));
			return false;
		}

		if (auto featureIt = data.find(kFeatureKey); featureIt != data.end() &&
													 (!featureIt->is_string() || featureIt->get<std::string>() != featureShortName)) {
			logger::error("[SceneSettings] Refusing to relabel overwrite file '{}' from another feature", Util::PathHelpers::ToUtf8(path));
			return false;
		}
		data[kFeatureKey] = featureShortName;
		auto& metadata = data[kMetadataKey];
		if (!metadata.is_null() && !metadata.is_object()) {
			logger::error("[SceneSettings] Refusing to replace invalid metadata in overwrite file '{}'", Util::PathHelpers::ToUtf8(path));
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
				Util::PathHelpers::ToUtf8(path));
			return false;
		}
		if (entryTransitions.is_null())
			entryTransitions = json::object();
		auto& entryDeletions = metadata[kMetadataEntryDeletionsKey];
		if (entryDeletions.is_null())
			entryDeletions = json::object();
		if (!entryDeletions.is_object())
			return false;
		for (const auto* entry : entries) {
			auto* node = GetOrCreateObjectAtPath(data, entry->settingPath);
			if (!node) {
				logger::error("[SceneSettings] Refusing to replace a non-object path in overwrite file '{}'",
					Util::PathHelpers::ToUtf8(path));
				return false;
			}
			(*node)[entry->settingKey] = entry->value;
			if (entry->deleted) {
				auto* deletionNode = GetOrCreateObjectAtPath(entryDeletions, entry->settingPath);
				if (!deletionNode)
					return false;
				(*deletionNode)[entry->settingKey] = true;
			} else {
				RemoveObjectValueAtPath(entryDeletions, entry->settingPath, 0, entry->settingKey);
			}
			if (entry->deleted || !entry->transitionSeconds) {
				RemoveObjectValueAtPath(entryTransitions, entry->settingPath, 0, entry->settingKey);
				continue;
			}
			auto* transitionNode = GetOrCreateObjectAtPath(entryTransitions, entry->settingPath);
			if (!transitionNode) {
				logger::error("[SceneSettings] Refusing to replace a non-object transition path in overwrite file '{}'",
					Util::PathHelpers::ToUtf8(path));
				return false;
			}
			(*transitionNode)[entry->settingKey] = *entry->transitionSeconds;
		}
		if (entryTransitions.empty())
			metadata.erase(kMetadataEntryTransitionsKey);
		if (entryDeletions.empty())
			metadata.erase(kMetadataEntryDeletionsKey);

		return Util::FileHelpers::WriteJsonAtomically(path, data, kOverwriteJsonIndent, "overwrite file");
	}

	bool ParseOverwriteFileEntries(const std::filesystem::path& filePath,
		SceneSettingsManager::SceneType allowedType, bool requireNumeric,
		std::vector<SceneSettingsManager::SettingEntry>& outEntries, FeatureSettingsCache* featureSettingsCache,
		std::optional<bool>* timeOfDayEnabled)
	{
		json data;
		if (!ReadBoundedSceneJson(filePath, data)) {
			logger::warn("[SceneSettings] Overwrite '{}' is invalid or exceeds {} bytes", Util::PathHelpers::ToUtf8(filePath),
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
		const json* entryDeletions = nullptr;
		if (auto metadataIt = data.find(kMetadataKey); metadataIt != data.end() && metadataIt->is_object()) {
			if (auto deletedIt = metadataIt->find(kMetadataEntryDeletionsKey); deletedIt != metadataIt->end()) {
				if (!deletedIt->is_object())
					return false;
				entryDeletions = &*deletedIt;
			}
			if (auto modeIt = metadataIt->find(kTimeOfDayEnabledKey); timeOfDayEnabled && modeIt != metadataIt->end()) {
				if (modeIt->is_boolean())
					*timeOfDayEnabled = modeIt->get<bool>();
				else
					logger::warn("[SceneSettings] Overwrite '{}' {} metadata must be boolean", Util::PathHelpers::ToUtf8(filePath),
						kTimeOfDayEnabledKey);
			}
			// Only the location layer transitions, so any other scene ignores the field.
			if (auto transitionsIt = metadataIt->find(kMetadataEntryTransitionsKey);
				allowedType == SSM::SceneType::Location && transitionsIt != metadataIt->end()) {
				if (transitionsIt->is_object())
					entryTransitions = &*transitionsIt;
				else
					logger::warn("[SceneSettings] Overwrite '{}' has invalid {} metadata", Util::PathHelpers::ToUtf8(filePath),
						kMetadataEntryTransitionsKey);
			}
		}

		std::string featureShortName = data.value(kFeatureKey, "");
		if (featureShortName.empty()) {
			auto stem = Util::PathHelpers::ToUtf8(filePath.stem());
			auto lastUnderscore = stem.rfind('_');
			if (lastUnderscore != std::string::npos)
				featureShortName = stem.substr(lastUnderscore + 1);
		}

		if (!Feature::FindFeatureByShortName(featureShortName)) {
			logger::warn("[SceneSettings] Overwrite '{}' targets feature '{}', which is not loaded - skipping",
				Util::PathHelpers::ToUtf8(filePath), featureShortName);
			return false;
		}
		if (!SSM::IsFeatureAllowedForType(allowedType, featureShortName)) {
			logger::warn("[SceneSettings] Overwrite '{}' feature '{}' is not allowed for {} - skipping",
				Util::PathHelpers::ToUtf8(filePath), featureShortName, SSM::GetSceneTypeName(allowedType));
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
			if (const auto* deletionNode = entryDeletions ? GetObjectAtPath(*entryDeletions, settingPath) : nullptr)
				if (auto deletedIt = deletionNode->find(key); deletedIt != deletionNode->end()) {
					if (!deletedIt->is_boolean())
						return;
					entry.deleted = deletedIt->get<bool>();
				}
			if (const auto* transitionNode = entryTransitions ? GetObjectAtPath(*entryTransitions, settingPath) : nullptr)
				if (auto transitionIt = transitionNode->find(key); transitionIt != transitionNode->end()) {
					const auto seconds = transitionIt->is_number() ? std::optional{ transitionIt->get<float>() } : std::nullopt;
					if (seconds && std::isfinite(*seconds) && *seconds >= 0.0f && *seconds <= SSM::kMaxLocationTransitionSeconds)
						entry.transitionSeconds = seconds;
					else
						logger::warn("[SceneSettings] Overwrite '{}' transition for {} must be a number in 0..{}",
							Util::PathHelpers::ToUtf8(filePath), GetSettingLogName(featureShortName, settingPath, key),
							SSM::kMaxLocationTransitionSeconds);
				}
			entry.featureShortName = featureShortName;
			if (entry.deleted)
				entry.transitionSeconds.reset();
			entry.settingPath = settingPath;
			entry.settingKey = key;
			entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, key);
			entry.value = std::move(parsedValue);
			entry.originalValue = entry.value;
			entry.source = SSM::EntrySource::Overwrite;
			entry.sourceFilename = Util::PathHelpers::ToUtf8(filePath.filename());
			entry.sourcePath = filePath;
			outEntries.push_back(std::move(entry));
			foundAny = true;
		});
		return foundAny;
	}
}
