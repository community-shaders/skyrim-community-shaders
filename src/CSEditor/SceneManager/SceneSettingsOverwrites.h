#pragma once

#include "SceneSettingsManager.h"

#include "SceneSettingsInternal.h"

#include <filesystem>

/// Layout of the overwrite directory tree and the grouped JSON files inside it.
namespace SceneSettingsOverwrites
{
	bool HasOverwriteEntryForPeriod(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		const SceneSettingsManager::SettingEntry& candidate);

	bool AddOverwriteEntryIfUnique(std::vector<SceneSettingsManager::SettingEntry>& entries,
		SceneSettingsManager::SettingEntry&& entry, std::string_view context);

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

	/// Discovered entries keep the exact file they came from; authored ones derive it from their period.
	std::filesystem::path GetOverwriteFilePath(const std::filesystem::path& baseDir,
		const SceneSettingsManager::SettingEntry& entry);

	std::string GetOverwriteTypeDescription(std::string_view sceneLabel,
		SceneSettingsManager::TimeOfDayPeriod period);

	std::filesystem::path GetSceneOverwritePath(SceneSettingsManager::SceneType type,
		const SceneSettingsManager::SettingEntry& entry);

	std::filesystem::path GetWeatherOverwritePath(RE::FormID weatherId, const SceneSettingsManager::SettingEntry& entry);

	std::filesystem::path GetLocationOverwritePath(std::string_view formKey,
		const SceneSettingsManager::SettingEntry& entry);

	/** @brief Merges entries into a grouped overwrite file.
	 *  @param allowedRoot Scene overwrite root the file must sit strictly inside; anything else is refused. */
	bool WriteGroupedOverwriteFile(const std::filesystem::path& allowedRoot, const std::filesystem::path& path,
		const std::string& featureShortName, const std::string& overwriteType,
		const std::vector<const SceneSettingsManager::SettingEntry*>& entries,
		const json& extraMetadata = json::object());

	bool RemoveSettingFromOverwriteFile(const std::filesystem::path& path,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

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
