#include "SceneSettingsManager.h"

#include "SceneSettingsInternal.h"
#include "SceneSettingsLocationTargets.h"
#include "SceneSettingsOverwrites.h"
#include "Utils/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <numeric>

using namespace SceneSettingsInternal;
using namespace SceneSettingsOverwrites;
using namespace SceneSettingsLocationTargets;

void SceneSettingsManager::DiscoverOverwrites(SceneType type)
{
	if (!IsEntryListSceneType(type))
		return;
	const auto previousEntryCount = GetEntries(type).size();
	const auto basePath = GetOverwritesPath(type);
	if (type == SceneType::TimeOfDay) {
		for (auto period : kPeriods)
			DiscoverOverwritesInDir(type, GetOverwriteDir(basePath, period), period);
	} else {
		DiscoverOverwritesInDir(type, basePath);
	}

	if (GetEntries(type).size() != previousEntryCount)
		BumpEntryPresentationRevision();
}

void SceneSettingsManager::DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir, TimeOfDayPeriod period)
{
	auto typeName = GetSceneTypeName(type);

	std::error_code ec;
	if (!std::filesystem::exists(dir, ec))
		return;

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, dir.string());

	bool requireNumeric = (type == SceneType::TimeOfDay);
	auto& vec = GetEntriesMut(type);
	int filesFound = 0, overwritesLoaded = 0;
	FeatureSettingsCache featureSettingsCache;

	for (const auto& filePath : GetSortedJsonFiles(dir, std::format("{} overwrite files", typeName))) {
		filesFound++;
		try {
			std::vector<SettingEntry> parsedEntries;
			if (!ParseOverwriteFileEntries(filePath, type, requireNumeric, parsedEntries, &featureSettingsCache))
				continue;
			for (auto& entry : parsedEntries) {
				entry.period = period;
				if (AddOverwriteEntryIfUnique(vec, std::move(entry), typeName))
					overwritesLoaded++;
			}
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, filePath.filename().string(), e.what());
		}
	}

	if (filesFound > 0)
		logger::info("[SceneSettings] {} overwrite scan: {} files, {} loaded", typeName, filesFound, overwritesLoaded);
}

std::vector<std::string> SceneSettingsManager::GetOverwriteModNames()
{
	// Best-effort: unlike ExportPreset, nothing is written here, so a partial load still returns
	// whatever overwrites are already known rather than reporting nothing.
	TryEnsureWeatherDataLoaded();
	TryEnsureLocationDataLoaded();

	std::vector<std::string> names;
	const auto collect = [&](const std::vector<SettingEntry>& sourceEntries) {
		for (const auto& entry : sourceEntries) {
			if (entry.source != EntrySource::Overwrite)
				continue;
			auto name = GetOverwriteModName(entry);
			if (!name.empty() && std::ranges::find(names, name) == names.end())
				names.push_back(std::move(name));
		}
	};

	for (const auto& [type, sourceEntries] : entries)
		collect(sourceEntries);
	for (const auto& [weatherId, config] : weatherSceneConfigs)
		collect(config.entries);
	for (const auto& [configKey, config] : locationSceneConfigs)
		collect(config.entries);
	return names;
}

std::vector<std::filesystem::path> SceneSettingsManager::FindPresetFiles(const std::string& modName) const
{
	std::vector<std::filesystem::path> found;
	if (modName.empty())
		return found;
	const auto prefix = modName + "_";

	const auto sweepDir = [&](const std::filesystem::path& directory) {
		std::error_code ec;
		if (!std::filesystem::exists(directory, ec))
			return;
		for (const auto& path : GetSortedJsonFiles(directory, "preset files"))
			if (path.filename().string().starts_with(prefix))
				found.push_back(path);
	};
	const auto childDirectories = [](const std::filesystem::path& root, std::string_view context) {
		std::error_code ec;
		return std::filesystem::exists(root, ec) ?
		           GetSortedDirectoryPaths(root, true, context) :
		           std::vector<std::filesystem::path>{};
	};

	sweepDir(GetOverwritesPath(SceneType::InteriorOnly));
	for (auto period : kPeriods)
		sweepDir(GetOverwriteDir(GetOverwritesPath(SceneType::TimeOfDay), period));
	// A weather or location keeps both saved sets on disk, so the preset claims its flat and per-period files.
	const auto sweepSetDirs = [&](const std::filesystem::path& sceneDir) {
		ForEachOverwriteSetDir(sceneDir, [&](const std::filesystem::path& directory, TimeOfDayPeriod) { sweepDir(directory); });
	};
	for (const auto& weatherDir : childDirectories(GetWeatherOverwritesDir(), "weather overwrite directories"))
		sweepSetDirs(weatherDir);
	for (const auto& locationDir : childDirectories(GetLocationOverwritesDir(), "location overwrite directories"))
		sweepSetDirs(locationDir);

	std::error_code ec;
	if (const auto metadataPath = GetPresetMetadataPath(modName);
		!IsReservedPresetName(modName) && std::filesystem::exists(metadataPath, ec))
		found.push_back(metadataPath);
	return found;
}

bool SceneSettingsManager::ExportPreset(const std::string& modName, const std::string& version)
{
	if (!IsValidPresetVersion(version)) {
		logger::error("[SceneSettings] Preset '{}' not exported: version '{}' is not MAJOR.MINOR.PATCH", modName, version);
		return false;
	}
	// Weather and location configs load lazily; baking before they exist would sweep their files and
	// write nothing back.
	if (!TryEnsureWeatherDataLoaded() || !TryEnsureLocationDataLoaded()) {
		logger::error("[SceneSettings] Preset '{}' not exported: weather or location data is not loaded", modName);
		return false;
	}

	const auto safeModName = Util::FileHelpers::SanitizeFileName(modName);
	if (safeModName.empty() || IsReservedPresetName(safeModName))
		return false;

	/// One output file: the root it must stay inside, the type description its metadata carries, and the
	/// entries baked into it.
	struct PresetFile
	{
		std::filesystem::path allowedRoot;
		std::string typeDescription;
		json extraMetadata = json::object();
		std::vector<const SettingEntry*> entries;
	};
	std::map<std::pair<std::filesystem::path, std::string>, PresetFile> files;

	const auto bakeContext = [&](const SceneContextId& context, const std::vector<SettingEntry>& sourceEntries,
								 const std::filesystem::path& allowedRoot, const std::filesystem::path& baseDir,
								 std::string_view sceneLabel, const json& extraMetadata = json::object()) {
		const auto directory = GetOverwriteDir(baseDir, context.period);
		for (const auto& [identity, entry] : BuildEffectiveContextEntries(sourceEntries, context)) {
			auto& file = files[{ directory, identity.featureShortName }];
			file.allowedRoot = allowedRoot;
			file.typeDescription = GetOverwriteTypeDescription(sceneLabel, context.period);
			file.extraMetadata = extraMetadata;
			file.entries.push_back(entry);
		}
	};

	const auto interiorRoot = GetOverwritesPath(SceneType::InteriorOnly);
	bakeContext({ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count },
		GetEntries(SceneType::InteriorOnly), interiorRoot, interiorRoot, "Interior Only");

	const auto timeOfDayRoot = GetOverwritesPath(SceneType::TimeOfDay);
	for (auto period : kPeriods)
		bakeContext({ .type = SceneContextType::TimeOfDay, .period = period },
			GetEntries(SceneType::TimeOfDay), timeOfDayRoot, timeOfDayRoot, "Time of Day");

	// Both saved sets ship; the metadata carries the mode that picks between them.
	const auto bakeSceneSets = [&](SceneContextId context, const PeriodicSceneConfig& config,
								   const std::filesystem::path& allowedRoot, const std::filesystem::path& sceneDir,
								   std::string_view sceneLabel, json metadata = json::object()) {
		metadata[kTimeOfDayEnabledKey] = config.timeOfDayEnabled;
		bakeContext(context, config.entries, allowedRoot, sceneDir, sceneLabel, metadata);
		for (auto period : kPeriods) {
			context.period = period;
			bakeContext(context, config.entries, allowedRoot, sceneDir, sceneLabel, metadata);
		}
	};

	const auto weatherRoot = GetWeatherOverwritesDir();
	for (const auto& [weatherId, config] : weatherSceneConfigs)
		bakeSceneSets({ .type = SceneContextType::Weather, .weatherId = weatherId }, config,
			weatherRoot, weatherRoot / Util::FormIdToSpid(weatherId), "Weather");

	const auto locationRoot = GetLocationOverwritesDir();
	for (const auto& [configKey, config] : locationSceneConfigs) {
		const auto* targetDescription = GetLocationTargetTypeName(config.type);
		bakeSceneSets({ .type = SceneContextType::Location,
						  .locationType = config.type,
						  .locationFormKey = config.formKey },
			config, locationRoot, locationRoot / config.formKey, targetDescription,
			json{ { "targetType", targetDescription },
				{ "targetName", config.name },
				{ "coc", config.cocCode } });
	}

	bool wroteAll = true;
	// The sweep runs only once every output is known, so a failure above costs nothing on disk. A file
	// that survives it would be merged into rather than replaced, silently reviving a deleted setting.
	for (const auto& path : FindPresetFiles(safeModName)) {
		std::error_code ec;
		std::filesystem::remove(path, ec);
		if (ec) {
			logger::error("[SceneSettings] Could not remove stale preset file '{}': {}", path.string(), ec.message());
			wroteAll = false;
		}
	}

	for (const auto& [key, file] : files) {
		const auto& [directory, featureShortName] = key;
		const auto path = directory / std::format("{}_{}.json", safeModName, featureShortName);
		if (!WriteGroupedOverwriteFile(file.allowedRoot, path, featureShortName, file.typeDescription, file.entries,
				file.extraMetadata)) {
			logger::error("[SceneSettings] Preset '{}' failed to write '{}'", safeModName, path.string());
			wroteAll = false;
		}
	}

	const json metadata{ { kPresetMetadataKey,
		{ { kPresetMetadataNameKey, safeModName }, { kPresetMetadataVersionKey, version },
			{ kTimeOfDayTransitionHoursKey, timeOfDayTransitionHours } } } };
	if (!WriteJsonAtomically(GetPresetMetadataPath(safeModName), metadata, kOverwriteJsonIndent, "preset metadata")) {
		logger::error("[SceneSettings] Preset '{}' failed to write its metadata file", safeModName);
		wroteAll = false;
	}
	DiscoverPresetMetadata();

	logger::info("[SceneSettings] Exported preset '{}' as {} file(s)", safeModName, files.size());
	return wroteAll;
}

void SceneSettingsManager::DiscoverPresetMetadata()
{
	presetMetadata.clear();
	const auto root = Util::PathHelpers::GetSceneSettingsPath();
	std::error_code ec;
	if (!std::filesystem::exists(root, ec)) {
		RefreshTimeOfDayTransitionHours();
		return;
	}

	for (const auto& path : GetSortedJsonFiles(root, "preset metadata files")) {
		if (IsReservedPresetName(path.stem().string()))
			continue;
		json data;
		if (!ReadBoundedSceneJson(path, data))
			continue;
		// Only a marked file is a preset; any other json dropped at the root is left alone.
		const auto metadataIt = data.find(kPresetMetadataKey);
		if (metadataIt == data.end())
			continue;

		// find() yields end() on a non-object, so a malformed block fails the same checks.
		const auto nameIt = metadataIt->find(kPresetMetadataNameKey);
		const auto versionIt = metadataIt->find(kPresetMetadataVersionKey);
		if (nameIt == metadataIt->end() || !nameIt->is_string() || nameIt->get_ref<const std::string&>().empty() ||
			versionIt == metadataIt->end() || !versionIt->is_string() ||
			!IsValidPresetVersion(versionIt->get_ref<const std::string&>())) {
			logger::warn("[SceneSettings] Preset metadata '{}' needs a name and a MAJOR.MINOR.PATCH version", path.string());
			continue;
		}
		presetMetadata.push_back({ .name = nameIt->get<std::string>(),
			.version = versionIt->get<std::string>(),
			.path = path,
			.transitionHours = ReadTimeOfDayTransitionHours(*metadataIt, path.string()) });
	}

	if (!presetMetadata.empty())
		logger::info("[SceneSettings] Found {} preset metadata file(s)", presetMetadata.size());
	RefreshTimeOfDayTransitionHours();
}

void SceneSettingsManager::DiscoverLocationOverwrites()
{
	const auto root = GetLocationOverwritesDir();
	std::error_code ec;
	if (!std::filesystem::exists(root, ec))
		return;
	for (const auto& directory : GetSortedDirectoryPaths(root, true, "location overwrite directories"))
		DiscoverLocationOverwritesForTarget(directory);
}

void SceneSettingsManager::DiscoverLocationOverwritesForTarget(const std::filesystem::path& targetDir)
{
	const auto formKey = targetDir.filename().string();
	if (formKey.empty())
		return;

	std::optional<LocationTargetType> resolvedType;
	std::string resolvedName;
	std::string resolvedCocCode;
	std::string canonicalFormKey = formKey;
	if (const auto formId = Util::SpidToFormId(formKey); formId != 0) {
		canonicalFormKey = Util::FormIdToSpid(formId);
		if (auto* form = RE::TESForm::LookupByID(formId)) {
			if (form->GetFormType() == RE::FormType::WorldSpace)
				resolvedType = LocationTargetType::Worldspace;
			else if (IsLocationTypeKeyword(form->As<RE::BGSKeyword>()))
				resolvedType = LocationTargetType::LocationType;
			else if (form->GetFormType() == RE::FormType::Region)
				resolvedType = LocationTargetType::Region;
			else if (form->GetFormType() == RE::FormType::Location)
				resolvedType = LocationTargetType::Location;
			else if (form->GetFormType() == RE::FormType::Cell)
				resolvedType = LocationTargetType::Cell;
			else {
				logger::warn("[SceneSettings] Location overwrite target '{}' is not a worldspace, location type, region, location, or cell",
					formKey);
				return;
			}
			resolvedName = Util::GetFormDisplayName(formId);
			if (*resolvedType == LocationTargetType::Cell)
				resolvedCocCode = Util::GetFormEditorID(form);
		}
	}

	FeatureSettingsCache featureSettingsCache;
	ForEachOverwriteSetDir(targetDir, [&](const std::filesystem::path& directory, TimeOfDayPeriod period) {
		for (const auto& filePath : GetSortedJsonFiles(directory, "location overwrite files")) {
			try {
				json data;
				if (!ReadBoundedSceneJson(filePath, data)) {
					logger::warn("[SceneSettings] Location overwrite '{}' is invalid or exceeds {} bytes",
						filePath.string(), kMaxSceneOverwriteFileSize);
					continue;
				}

				std::optional<LocationTargetType> metadataType;
				std::string metadataName;
				std::string metadataCocCode;
				if (auto metadataIt = data.find(kMetadataKey); metadataIt != data.end()) {
					if (!metadataIt->is_object()) {
						logger::warn("[SceneSettings] Location overwrite '{}' metadata must be an object",
							filePath.string());
						continue;
					}
					const auto metadataContext = std::format("Location overwrite '{}' metadata", filePath.string());
					std::string targetType;
					if (!ReadOptionalStringField(*metadataIt, "targetType", targetType, metadataContext) ||
						!ReadOptionalStringField(*metadataIt, "targetName", metadataName, metadataContext) ||
						!ReadOptionalStringField(*metadataIt, "coc", metadataCocCode, metadataContext))
						continue;
					if (targetType == kLegacyLocationTypeName)
						metadataType = LocationTargetType::LocationType;
					for (const auto type : kLocationTargetTypes)
						if (targetType == GetLocationTargetTypeName(type))
							metadataType = type;
					if (!metadataType && !targetType.empty()) {
						logger::warn("[SceneSettings] {} has invalid targetType '{}'", metadataContext, targetType);
						continue;
					}
				}
				if (resolvedType && metadataType && *resolvedType != *metadataType) {
					logger::warn("[SceneSettings] Location overwrite '{}' targetType does not match resolved form '{}'",
						filePath.string(), formKey);
					continue;
				}
				const auto targetType = resolvedType ? resolvedType : metadataType;
				if (!targetType) {
					logger::warn("[SceneSettings] Location overwrite '{}' has no resolvable target type",
						filePath.string());
					continue;
				}

				std::vector<SettingEntry> parsedEntries;
				std::optional<bool> timeOfDayEnabled;
				if (!ParseOverwriteFileEntries(data, filePath, SceneType::Location, period != TimeOfDayPeriod::Count,
						parsedEntries, &featureSettingsCache, &timeOfDayEnabled))
					continue;

				// Created only once a file yields entries, so a rejected file leaves no empty target behind.
				auto& config = GetLocationConfigMut(*targetType, canonicalFormKey,
					!metadataName.empty() ? metadataName : resolvedName);
				if (!metadataCocCode.empty())
					config.cocCode = metadataCocCode;
				else if (!resolvedCocCode.empty())
					config.cocCode = resolvedCocCode;
				if (!config.overwriteTimeOfDayEnabled)
					config.overwriteTimeOfDayEnabled = timeOfDayEnabled;
				for (auto& entry : parsedEntries) {
					entry.period = period;
					AddOverwriteEntryIfUnique(config.entries, std::move(entry), "location");
				}
			} catch (const std::exception& e) {
				logger::error("[SceneSettings] Failed to load location overwrite '{}': {}",
					filePath.filename().string(), e.what());
			}
		}
	});
}

void SceneSettingsManager::DiscoverWeatherOverwrites()
{
	const auto countWeatherEntries = [this] {
		return std::accumulate(weatherSceneConfigs.begin(), weatherSceneConfigs.end(), size_t{ 0 },
			[](size_t total, const auto& config) { return total + config.second.entries.size(); });
	};

	const auto previousEntryCount = countWeatherEntries();
	auto baseDir = GetWeatherOverwritesDir();
	std::error_code ec;
	if (!std::filesystem::exists(baseDir, ec))
		return;

	logger::info("[SceneSettings] Discovering weather overwrites in: {}", baseDir.string());

	for (const auto& weatherDirectory : GetSortedDirectoryPaths(baseDir, true, "weather overwrite directories")) {
		auto folderName = weatherDirectory.filename().string();
		RE::FormID weatherId = Util::SpidToFormId(folderName);
		if (weatherId == 0) {
			logger::warn("[SceneSettings] Weather overwrite folder '{}' could not be resolved - skipping", folderName);
			continue;
		}

		DiscoverWeatherOverwritesForSpid(weatherId, weatherDirectory);
	}

	if (countWeatherEntries() != previousEntryCount)
		BumpEntryPresentationRevision();
}

void SceneSettingsManager::DiscoverWeatherOverwritesForSpid(RE::FormID weatherId, const std::filesystem::path& weatherDir)
{
	FeatureSettingsCache featureSettingsCache;

	ForEachOverwriteSetDir(weatherDir, [&](const std::filesystem::path& directory, TimeOfDayPeriod period) {
		for (const auto& filePath : GetSortedJsonFiles(directory, "weather overwrite files")) {
			try {
				std::vector<SettingEntry> parsedEntries;
				std::optional<bool> timeOfDayEnabled;
				if (!ParseOverwriteFileEntries(filePath, SceneType::TimeOfDay, true, parsedEntries, &featureSettingsCache,
						&timeOfDayEnabled))
					continue;
				// Created only once a file yields entries, so a rejected file leaves no empty weather behind.
				auto& config = GetWeatherConfigMut(weatherId);
				if (!config.overwriteTimeOfDayEnabled)
					config.overwriteTimeOfDayEnabled = timeOfDayEnabled;
				for (auto& entry : parsedEntries) {
					entry.period = period;
					AddOverwriteEntryIfUnique(config.entries, std::move(entry), "weather");
				}
			} catch (const std::exception& e) {
				logger::error("[SceneSettings] Failed to load weather overwrite '{}': {}", filePath.filename().string(), e.what());
			}
		}
	});
}
