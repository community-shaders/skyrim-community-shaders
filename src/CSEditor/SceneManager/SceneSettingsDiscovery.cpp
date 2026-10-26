#include "SceneSettingsManager.h"

#include "BaselineManager.h"
#include "CSEditor/FormEditSources.h"
#include "Features/CSEditor.h"
#include "Features/Effects11.h"
#include "Features/Effects11/PresetManager.h"
#include "PresetExportTransaction.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "SceneSettingsInternal.h"
#include "SceneSettingsLocationTargets.h"
#include "SceneSettingsOverwrites.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>

using namespace SceneSettingsInternal;
using namespace SceneSettingsOverwrites;
using namespace SceneSettingsLocationTargets;

void SceneSettingsManager::DiscoverOverwrites(SceneType type)
{
	const auto packRoot = GetActiveScenePackRoot();
	if (!IsEntryListSceneType(type) || packRoot.empty())
		return;
	const auto previousEntryCount = GetEntries(type).size();
	const auto basePath = GetOverwritesPath(type, packRoot);
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

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, Util::PathHelpers::ToUtf8(dir));

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
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, Util::PathHelpers::ToUtf8(filePath.filename()), e.what());
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

std::vector<std::filesystem::path> SceneSettingsManager::FindPresetFiles(const std::string& packId)
{
	std::vector<std::filesystem::path> found;
	if (packId.empty())
		return found;
	const auto packRoot = Util::PathHelpers::GetUnifiedPackPath(packId);
	const auto prefix = packId + "_";

	const auto sweepDir = [&](const std::filesystem::path& directory) {
		std::error_code ec;
		if (!std::filesystem::exists(directory, ec))
			return;
		for (const auto& path : GetSortedJsonFiles(directory, "preset files"))
			if (Util::PathHelpers::ToUtf8(path.filename()).starts_with(prefix))
				found.push_back(path);
	};
	const auto childDirectories = [](const std::filesystem::path& root, std::string_view context) {
		std::error_code ec;
		return std::filesystem::exists(root, ec) ?
		           GetSortedDirectoryPaths(root, true, context) :
		           std::vector<std::filesystem::path>{};
	};

	sweepDir(GetOverwritesPath(SceneType::InteriorOnly, packRoot));
	for (auto period : kPeriods)
		sweepDir(GetOverwriteDir(GetOverwritesPath(SceneType::TimeOfDay, packRoot), period));
	// A weather or location keeps both saved sets on disk, so the preset claims its flat and per-period files.
	const auto sweepSetDirs = [&](const std::filesystem::path& sceneDir) {
		ForEachOverwriteSetDir(sceneDir, [&](const std::filesystem::path& directory, TimeOfDayPeriod) { sweepDir(directory); });
	};
	for (const auto& weatherDir : childDirectories(GetWeatherOverwritesDir(packRoot), "weather overwrite directories"))
		sweepSetDirs(weatherDir);
	for (const auto& locationDir : childDirectories(GetLocationOverwritesDir(packRoot), "location overwrite directories"))
		sweepSetDirs(locationDir);
	return found;
}

bool SceneSettingsManager::ExportPreset(const PresetExportInfo& info)
try {
	if (!IsValidPresetVersion(info.version)) {
		logger::error("[SceneSettings] Preset '{}' not exported: version '{}' is not MAJOR.MINOR.PATCH", info.name, info.version);
		return false;
	}

	const bool exportEffects11 = info.type == PresetType::E11;
	const bool hasScenePayload = HasAnyUserEntries() || !GetOverwriteModNames().empty();
	// CS exports always bake scene files; E11 exports bake them when any scene layer exists. A Baseline
	// export carries base settings only, so it leaves the pack's scene files alone.
	bool exportScene = info.type == PresetType::CS || (info.type == PresetType::E11 && hasScenePayload);

	if (exportEffects11 && (!globals::features::effects11.loaded || !PresetManager::GetSingleton().CanExportActivePreset())) {
		logger::error("[SceneSettings] Preset '{}' not exported: Effects 11 has no valid active ENB layout to copy", info.name);
		return false;
	}

	// Weather and location configs load lazily; baking before they exist would sweep their files and
	// write nothing back. E11-only exports can skip the bake when that data is unavailable.
	if (exportScene) {
		if (!TryEnsureWeatherDataLoaded() || !TryEnsureLocationDataLoaded()) {
			if (!exportEffects11) {
				logger::error("[SceneSettings] Preset '{}' not exported: weather or location data is not loaded", info.name);
				return false;
			}
			logger::warn("[SceneSettings] Preset '{}': scene bake skipped; weather or location data is not loaded", info.name);
			exportScene = false;
		}
	}

	const auto safeModName = Util::FileHelpers::SanitizeFileName(info.name);
	if (safeModName.empty() || IsReservedPresetName(safeModName))
		return false;
	if (const auto* pack = UnifiedPresetCatalog::GetSingleton().FindPack(safeModName); pack && !pack->AcceptsExport(info.type)) {
		logger::error("[SceneSettings] Preset '{}' not exported: the existing pack is not a {} preset",
			safeModName, UnifiedPresetCatalog::GetPresetTypeName(info.type));
		return false;
	}

	const auto targetRoot = Util::PathHelpers::GetUnifiedPackPath(safeModName);
	PresetExportTransaction transaction(targetRoot);
	if (!transaction.Prepare())
		return false;
	const auto& packRoot = transaction.Root();
	// Merged into rather than replaced, so fields other backends own (effects11, backends) survive.
	std::string manifestError;
	auto manifest = UnifiedPresetCatalog::ReadPackManifest(packRoot, &manifestError);
	if (!manifestError.empty()) {
		logger::error("[SceneSettings] Preset '{}' not exported: {}", safeModName, manifestError);
		return false;
	}

	const auto copyPackFile = [&](const std::filesystem::path& source, const std::filesystem::path& relativeDest)
		-> std::optional<std::string> {
		std::error_code ec;
		if (source.empty() || !std::filesystem::is_regular_file(source, ec))
			return std::nullopt;
		const auto dest = packRoot / relativeDest;
		// Re-exporting the active pack finds its own files as sources.
		if (std::filesystem::equivalent(source, dest, ec))
			return relativeDest.generic_string();
		std::filesystem::create_directories(dest.parent_path(), ec);
		std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) {
			logger::error("[SceneSettings] Preset '{}' failed to copy '{}' -> '{}': {}",
				safeModName, Util::PathHelpers::ToUtf8(source), Util::PathHelpers::ToUtf8(dest), ec.message());
			return std::nullopt;
		}
		return relativeDest.generic_string();
	};

	// Scene values naming a file ship that file inside the pack; baked entries point at the copy.
	constexpr std::string_view kPackFileDir = "files";
	std::deque<SettingEntry> packedFileEntries;
	std::map<std::filesystem::path, std::optional<std::string>> packedFileBySource;
	std::set<std::string> usedFileDests;
	bool fileCopyFailed = false;
	size_t sceneFileCount = 0;
	bool wroteAll = true;

	// Read before any sweep: exporting into the active pack reads its own Forms files as sources.
	const bool exportForms = info.type != PresetType::Baseline;
	std::vector<FormEditSources::FormEdit> formEdits;
	bool formSourcesRead = true;
	if (exportForms)
		wroteAll = formSourcesRead = FormEditSources::ReadEffectiveFormEdits(formEdits);

	const auto removeStaleFiles = [&](const std::vector<std::filesystem::path>& paths) {
		for (const auto& path : paths) {
			std::error_code ec;
			if (!Util::IsPathWithinDirectory(targetRoot, path) && !Util::IsPathWithinDirectory(packRoot, path))
				throw std::runtime_error("Preset cleanup path is outside the export roots");
			const auto stagedPath = Util::IsPathWithinDirectory(targetRoot, path) ?
			                            packRoot / path.lexically_relative(targetRoot) :
			                            path;
			std::filesystem::remove(stagedPath, ec);
			if (ec) {
				logger::error("[SceneSettings] Could not remove stale preset file '{}': {}", Util::PathHelpers::ToUtf8(path), ec.message());
				wroteAll = false;
			}
		}
	};

	if (exportScene) {
		const auto resolveEntryFileSource = [](const SettingEntry* entry) -> std::optional<std::filesystem::path> {
			if (entry->deleted || !entry->value.is_string() || entry->value.get_ref<const std::string&>().empty())
				return std::nullopt;
			auto source = UnifiedPresetCatalog::GetSingleton().ResolveActivePackPath(entry->value.get<std::string>());
			std::error_code ec;
			if (!std::filesystem::is_regular_file(source, ec))
				return std::nullopt;
			return source;
		};
		const auto packFile = [&](const SettingEntry* entry) -> const SettingEntry* {
			const auto source = resolveEntryFileSource(entry);
			if (!source)
				return entry;
			auto [it, inserted] = packedFileBySource.try_emplace(*source);
			if (inserted) {
				auto dest = std::filesystem::path(kPackFileDir) / source->filename();
				for (int suffix = 2; usedFileDests.contains(Util::FixFilePath(dest.generic_string())); ++suffix)
					dest = std::filesystem::path(kPackFileDir) /
					       std::format("{}_{}{}", source->stem().string(), suffix, source->extension().string());
				usedFileDests.insert(Util::FixFilePath(dest.generic_string()));
				it->second = copyPackFile(*source, dest);
				fileCopyFailed |= !it->second;
			}
			if (!it->second)
				return entry;
			auto& packed = packedFileEntries.emplace_back(*entry);
			packed.value = *it->second;
			return &packed;
		};

		// A file the pack already ships keeps its path, so no other source may be copied over it.
		const auto reserveInPackSource = [&](const SettingEntry* entry) {
			const auto source = resolveEntryFileSource(entry);
			if (!source)
				return;
			std::error_code sourceError, rootError;
			const auto relative = std::filesystem::absolute(*source, sourceError).lexically_relative(std::filesystem::absolute(targetRoot, rootError));
			if (sourceError || rootError || relative.empty() || relative.generic_string().starts_with(".."))
				return;
			const auto relativeString = relative.generic_string();
			usedFileDests.insert(Util::FixFilePath(relativeString));
			packedFileBySource.try_emplace(*source, relativeString);
		};

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
			for (const auto& [identity, entry] : BuildEffectiveContextEntries(sourceEntries, context, true)) {
				auto& file = files[{ directory, identity.featureShortName }];
				file.allowedRoot = allowedRoot;
				file.typeDescription = GetOverwriteTypeDescription(sceneLabel, context.period);
				file.extraMetadata = extraMetadata;
				file.entries.push_back(entry);
			}
		};

		const auto interiorRoot = GetOverwritesPath(SceneType::InteriorOnly, packRoot);
		bakeContext({ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count },
			GetEntries(SceneType::InteriorOnly), interiorRoot, interiorRoot, "Interior Only");

		const auto timeOfDayRoot = GetOverwritesPath(SceneType::TimeOfDay, packRoot);
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

		const auto weatherRoot = GetWeatherOverwritesDir(packRoot);
		for (const auto& [weatherId, config] : weatherSceneConfigs)
			bakeSceneSets({ .type = SceneContextType::Weather, .weatherId = weatherId }, config,
				weatherRoot, weatherRoot / Util::FormIdToSpid(weatherId), "Weather");

		const auto locationRoot = GetLocationOverwritesDir(packRoot);
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

		// In-pack files are claimed across every output before any other source is named.
		for (const auto& [fileKey, file] : files)
			std::ranges::for_each(file.entries, reserveInPackSource);
		for (auto& [fileKey, file] : files)
			std::ranges::transform(file.entries, file.entries.begin(), packFile);

		if (fileCopyFailed)
			return false;
		// The sweep runs only once every output is known, so a failure above costs nothing on disk. A file
		// that survives it would be merged into rather than replaced, silently reviving a deleted setting.
		removeStaleFiles(FindPresetFiles(safeModName));

		for (const auto& [key, file] : files) {
			const auto& [directory, featureShortName] = key;
			const auto path = directory / std::format("{}_{}.json", safeModName, featureShortName);
			if (!WriteGroupedOverwriteFile(file.allowedRoot, path, featureShortName, file.typeDescription, file.entries,
					file.extraMetadata)) {
				logger::error("[SceneSettings] Preset '{}' failed to write '{}'", safeModName, Util::PathHelpers::ToUtf8(path));
				wroteAll = false;
			}
		}
		sceneFileCount = files.size();
	}

	if (exportForms) {
		// The sweep would delete an unreadable source with nothing to replace it.
		if (formSourcesRead)
			removeStaleFiles(FormEditSources::ListPackFormFiles(packRoot));
		const auto formsRoot = FormEditSources::GetPackFormsRoot(packRoot);
		for (const auto& edit : formEdits) {
			const auto path = FormEditSources::GetFilePath(formsRoot, edit.key);
			Util::FileHelpers::EnsureDirectoryExists(path.parent_path());
			if (!Util::FileHelpers::WriteJsonAtomically(path, edit.content, kOverwriteJsonIndent, "preset form edit")) {
				logger::error("[SceneSettings] Preset '{}' failed to write '{}'", safeModName, Util::PathHelpers::ToUtf8(path));
				wroteAll = false;
			}
		}
	}

	if (exportEffects11) {
		const auto effects11Root = packRoot / UnifiedPresetCatalog::kEffects11PackSubdir;
		if (Util::IsPathWithinDirectory(targetRoot, PresetManager::GetSingleton().GetActivePresetRoot()))
			transaction.TrackExternalWrites();
		if (!PresetManager::GetSingleton().ExportActivePresetTo(effects11Root, true)) {
			logger::error("[SceneSettings] Preset '{}' failed to export Effects 11 files into '{}'",
				safeModName, Util::PathHelpers::ToUtf8(effects11Root));
			wroteAll = false;
		} else {
			if (!manifest.contains("backends") || !manifest["backends"].is_object())
				manifest["backends"] = json::object();
			manifest["backends"]["effects11"] = true;
			manifest["effects11"] = json{ { "path", UnifiedPresetCatalog::kEffects11PackSubdir } };
		}
	}

	if (!manifest.contains(PackManifest::kNameKey))
		manifest[PackManifest::kNameKey] = safeModName;
	info.WriteDetails(manifest);
	// Base settings added to a CS or E11 pack do not turn it into a Baseline pack.
	if (info.type != PresetType::Baseline || !manifest.contains(UnifiedPresetCatalog::kPresetTypeKey))
		manifest[UnifiedPresetCatalog::kPresetTypeKey] = UnifiedPresetCatalog::GetPresetTypeName(info.type);
	if (info.type != PresetType::Baseline)
		for (size_t index = 0; index < kTransitionControlCount; ++index)
			manifest[kTransitionControlSpecs[index].key] = transitionControls[index];
	if (exportEffects11) {
		if (auto sceneControl = E11Handoff::ToManifest(); sceneControl.empty())
			manifest.erase(UnifiedPresetCatalog::kSceneControlKey);
		else
			manifest[UnifiedPresetCatalog::kSceneControlKey] = std::move(sceneControl);
	}

	// Artwork: newly picked files win; otherwise keep the manifest's paths unless the user cleared them.
	const auto exportArtwork = [&](const char* key, const std::filesystem::path& source, bool clear, std::string_view stem) {
		if (!source.empty()) {
			if (auto rel = copyPackFile(source, std::format("{}{}", stem, Util::PathHelpers::ToUtf8(source.extension()))))
				manifest[key] = *rel;
			else
				wroteAll = false;
		} else if (clear) {
			manifest.erase(key);
		}
	};
	exportArtwork(PackManifest::kLogoKey, info.logoSource, info.clearLogo, "logo");
	exportArtwork(PackManifest::kCoverKey, info.coverSource, info.clearCover, "cover");

	if (!info.screenshotSources.empty() || info.clearScreenshots) {
		const std::filesystem::path galleryDir = "gallery";
		std::error_code galleryEc;
		std::filesystem::remove_all(packRoot / galleryDir, galleryEc);
		if (galleryEc)
			return false;
		manifest.erase(PackManifest::kScreenshotsKey);
		json shots = json::array();
		for (size_t i = 0; i < info.screenshotSources.size(); ++i) {
			const auto& source = info.screenshotSources[i];
			if (auto written = copyPackFile(source, galleryDir / std::format("{:02}{}", i + 1, Util::PathHelpers::ToUtf8(source.extension()))))
				shots.push_back(*written);
			else
				wroteAll = false;
		}
		if (!shots.empty())
			manifest[PackManifest::kScreenshotsKey] = std::move(shots);
	}

	for (const auto& [featureName, settings] : info.baselines) {
		const auto path = packRoot / UnifiedPresetCatalog::kBaselineSubdir / (featureName + ".json");
		if (Util::FileHelpers::SanitizeFileName(featureName) != featureName ||
			!BaselineManager::GetSingleton()->IsValidDocument(settings, path))
			return false;
		auto document = BaselineManager::ReadExportTarget(path);
		if (!document)
			return false;
		if (info.replaceBaselines.contains(featureName))
			*document = settings;
		else
			document->update(settings, true);
		if (!BaselineManager::GetSingleton()->IsValidDocument(*document, path) ||
			!Util::FileHelpers::WriteJsonAtomically(path, *document, kOverwriteJsonIndent, "preset baseline"))
			return false;
	}
	for (const auto& [featureName, disabled] : info.disableAtBoot)
		manifest[UnifiedPresetCatalog::kDisableAtBootKey][featureName] = disabled;

	if (!Util::FileHelpers::WriteJsonAtomically(UnifiedPresetCatalog::GetPackManifestPath(packRoot), manifest, kOverwriteJsonIndent, "preset manifest")) {
		logger::error("[SceneSettings] Preset '{}' failed to write its manifest", safeModName);
		wroteAll = false;
	}

	if (!wroteAll || !transaction.Commit())
		return false;

	// The files have landed, so a reload that throws past here must not report the export as failed.
	try {
		std::error_code activeEc;
		const bool exportedIntoActivePack = std::filesystem::equivalent(targetRoot, GetActiveScenePackRoot(), activeEc);
		if (exportScene && exportedIntoActivePack)
			ReloadOverwrites();
		if (exportForms && exportedIntoActivePack) {
			// The pack now supplies these values; loose copies would shadow it.
			for (const auto& edit : formEdits) {
				std::error_code removeEc;
				std::filesystem::remove(FormEditSources::GetFilePath(Util::PathHelpers::GetCommunityShaderPath(), edit.key), removeEc);
			}
			CSEditor::ReloadFormEdits();
		}
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Preset '{}' was exported, but reloading it failed: {}", safeModName, e.what());
	}

	logger::info("[SceneSettings] Exported preset '{}' ({} scene file(s), {} form edit(s){}, type {}) to '{}'",
		safeModName, sceneFileCount, formEdits.size(), exportEffects11 ? ", Effects 11" : "",
		UnifiedPresetCatalog::GetPresetTypeName(info.type), Util::PathHelpers::ToUtf8(targetRoot));
	return true;
} catch (const std::exception& e) {
	logger::error("[SceneSettings] Preset export failed: {}", e.what());
	return false;
}

std::optional<SceneSettingsManager::PresetMetadata> SceneSettingsManager::ReadPresetMetadata(const std::filesystem::path& packRoot)
{
	const auto manifestPath = UnifiedPresetCatalog::GetPackManifestPath(packRoot);
	std::error_code ec;
	if (packRoot.empty() || !std::filesystem::exists(manifestPath, ec))
		return std::nullopt;

	const auto manifest = UnifiedPresetCatalog::ReadPackManifest(packRoot);
	PresetMetadata metadata{ PackManifest::Read(manifest, Util::PathHelpers::ToUtf8(packRoot.filename())), manifestPath };
	const auto context = Util::PathHelpers::ToUtf8(manifestPath);
	for (size_t index = 0; index < kTransitionControlCount; ++index)
		metadata.transitionControls[index] = ReadTransitionControl(manifest, kTransitionControlSpecs[index].key,
			static_cast<TransitionControl>(index), context);
	return metadata;
}

void SceneSettingsManager::DiscoverPresetMetadata()
{
	activePresetMetadata = ReadPresetMetadata(GetActiveScenePackRoot());
	if (activePresetMetadata)
		logger::info("[SceneSettings] Active scene preset: '{}'", activePresetMetadata->name);
	RefreshTransitionControls();
	// Features the pack hands back from Effects 11 are driven by this layer, so they follow the same pack
	UnifiedPresetCatalog::GetSingleton().LoadSceneControl();
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
	const auto formKey = Util::PathHelpers::ToUtf8(targetDir.filename());
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
						Util::PathHelpers::ToUtf8(filePath), kMaxSceneOverwriteFileSize);
					continue;
				}

				std::optional<LocationTargetType> metadataType;
				std::string metadataName;
				std::string metadataCocCode;
				if (auto metadataIt = data.find(kMetadataKey); metadataIt != data.end()) {
					if (!metadataIt->is_object()) {
						logger::warn("[SceneSettings] Location overwrite '{}' metadata must be an object",
							Util::PathHelpers::ToUtf8(filePath));
						continue;
					}
					const auto metadataContext = std::format("Location overwrite '{}' metadata", Util::PathHelpers::ToUtf8(filePath));
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
						Util::PathHelpers::ToUtf8(filePath), formKey);
					continue;
				}
				const auto targetType = resolvedType ? resolvedType : metadataType;
				if (!targetType) {
					logger::warn("[SceneSettings] Location overwrite '{}' has no resolvable target type",
						Util::PathHelpers::ToUtf8(filePath));
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
				MergeOverwriteFileEntries(config, std::move(parsedEntries), timeOfDayEnabled, period, "location");
			} catch (const std::exception& e) {
				logger::error("[SceneSettings] Failed to load location overwrite '{}': {}",
					Util::PathHelpers::ToUtf8(filePath.filename()), e.what());
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

	logger::info("[SceneSettings] Discovering weather overwrites in: {}", Util::PathHelpers::ToUtf8(baseDir));

	for (const auto& weatherDirectory : GetSortedDirectoryPaths(baseDir, true, "weather overwrite directories")) {
		auto folderName = Util::PathHelpers::ToUtf8(weatherDirectory.filename());
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
				MergeOverwriteFileEntries(GetWeatherConfigMut(weatherId), std::move(parsedEntries), timeOfDayEnabled,
					period, "weather");
			} catch (const std::exception& e) {
				logger::error("[SceneSettings] Failed to load weather overwrite '{}': {}", Util::PathHelpers::ToUtf8(filePath.filename()), e.what());
			}
		}
	});
}
