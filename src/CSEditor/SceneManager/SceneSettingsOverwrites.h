#pragma once

#include "SceneSettingsManager.h"

#include "SceneSettingsInternal.h"

#include <filesystem>

/// Layout of the overwrite directory tree and the grouped JSON files inside it.
namespace SceneSettingsOverwrites
{
	/** @brief Whether an overwrite entry already holds the candidate's address and period. */
	bool HasOverwriteEntryForPeriod(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		const SceneSettingsManager::SettingEntry& candidate);

	/** @brief Appends the entry unless its address and period are taken; files scan in order, so the first wins. */
	bool AddOverwriteEntryIfUnique(std::vector<SceneSettingsManager::SettingEntry>& entries,
		SceneSettingsManager::SettingEntry&& entry, std::string_view context);

	/** @brief Adds one overwrite file's entries to a config under its folder's period; the first file's mode wins. */
	void MergeOverwriteFileEntries(SceneSettingsManager::PeriodicSceneConfig& config,
		std::vector<SceneSettingsManager::SettingEntry>&& entries, std::optional<bool> timeOfDayEnabled,
		SceneSettingsManager::TimeOfDayPeriod period, std::string_view context);

	/// Per-period overwrites live in a period subfolder of their scene's directory.
	std::filesystem::path GetOverwriteDir(const std::filesystem::path& baseDir,
		SceneSettingsManager::TimeOfDayPeriod period);

	/** @brief Visits each period subfolder that exists, then the scene's flat overwrite directory.
	 *  Periods go first, as upstream scans them, so the first `timeOfDayEnabled` metadata found agrees. */
	template <class Visit>
	void ForEachOverwriteSetDir(const std::filesystem::path& sceneDir, Visit&& visit)
	{
		std::error_code ec;
		for (const auto period : SceneSettingsManager::kPeriods)
			if (const auto periodDir = GetOverwriteDir(sceneDir, period); std::filesystem::exists(periodDir, ec))
				visit(periodDir, period);
		if (std::filesystem::exists(sceneDir, ec))
			visit(sceneDir, SceneSettingsManager::TimeOfDayPeriod::Count);
	}

	/** @brief "Label - Period" for a per-period overwrite, else just the label. */
	std::string GetOverwriteTypeDescription(std::string_view sceneLabel,
		SceneSettingsManager::TimeOfDayPeriod period);

	/** @brief Merges entries into a grouped overwrite file.
	 *  @param allowedRoot Scene overwrite root the file must sit strictly inside; anything else is refused. */
	bool WriteGroupedOverwriteFile(const std::filesystem::path& allowedRoot, const std::filesystem::path& path,
		const std::string& featureShortName, const std::string& overwriteType,
		const std::vector<const SceneSettingsManager::SettingEntry*>& entries,
		const json& extraMetadata = json::object());

	/** @param timeOfDayEnabled Receives the file's `timeOfDayEnabled` metadata, if it has any. */
	bool ParseOverwriteFileEntries(const std::filesystem::path& filePath,
		SceneSettingsManager::SceneType allowedType, bool requireNumeric,
		std::vector<SceneSettingsManager::SettingEntry>& outEntries,
		SceneSettingsInternal::FeatureSettingsCache* featureSettingsCache,
		std::optional<bool>* timeOfDayEnabled = nullptr);

	/** @brief Parses an overwrite already read by the caller; `filePath` only names and sources the entries. */
	bool ParseOverwriteFileEntries(const json& data, const std::filesystem::path& filePath,
		SceneSettingsManager::SceneType allowedType, bool requireNumeric,
		std::vector<SceneSettingsManager::SettingEntry>& outEntries,
		SceneSettingsInternal::FeatureSettingsCache* featureSettingsCache,
		std::optional<bool>* timeOfDayEnabled = nullptr);
}
