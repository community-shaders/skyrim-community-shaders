#include "PostProcessingPresets.h"

#include <format>
#include <optional>
#include <string>
#include <utility>

#include "BaselineManager.h"
#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "Features/PostProcessing.h"
#include "Globals.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsCatalog.h"

namespace
{
	/// Where Post Processing kept its own presets before they became Baseline packs.
	constexpr const char* kLegacyPresetDir = "Data/SKSE/Plugins/CommunityShaders/PostProcessing";
	/// The shipped defaults file, now baked into the sub-features' Settings initializers. Older
	/// installs may still have it; it is the stock look, so it is not converted into a preset.
	constexpr const char* kLegacyDefaultName = "default";
	constexpr const char* kMigratedSuffix = ".migrated";
	constexpr int kBaselineIndent = 4;

	std::filesystem::path BaselinePath(const std::filesystem::path& packRoot)
	{
		return packRoot / UnifiedPresetCatalog::kBaselineSubdir /
		       std::format("{}.json", PostProcessingPresets::kFeatureShortName);
	}

	/// A Baseline file carries settings only: no legacy preset name and no metadata keys.
	json ToBaselineDocument(json settings)
	{
		if (!settings.is_object())
			return json::object();
		settings.erase("preset_name");
		Util::Settings::EraseMetadataKeys(settings);
		return settings;
	}

	/// The same checks a Baseline file meets when the catalog loads it, so nothing is written that would be rejected.
	bool IsValidBaseline(const json& document, const std::filesystem::path& path)
	{
		auto* baselines = BaselineManager::GetSingleton();
		return baselines && baselines->IsValidDocument(document, path);
	}

	bool WriteDocument(const std::filesystem::path& packRoot, const json& document)
	{
		const auto path = BaselinePath(packRoot);
		if (!IsValidBaseline(document, path))
			return false;
		Util::FileHelpers::EnsureDirectoryExists(path.parent_path());
		return Util::FileHelpers::WriteJsonAtomically(path, document, kBaselineIndent, "post processing baseline");
	}
}

void PostProcessingPresets::MigrateLegacyPresets()
{
	static bool migrated = false;
	if (std::exchange(migrated, true))
		return;

	const std::filesystem::path legacyDir = kLegacyPresetDir;
	std::error_code ec;
	if (!std::filesystem::is_directory(legacyDir, ec))
		return;

	for (const auto& entry : std::filesystem::directory_iterator(legacyDir, ec)) {
		const auto& source = entry.path();
		if (!entry.is_regular_file(ec) || source.extension() != ".json" ||
			_stricmp(Util::PathHelpers::ToUtf8(source.stem()).c_str(), kLegacyDefaultName) == 0)
			continue;

		const auto name = Util::PathHelpers::ToUtf8(source.stem());
		const auto packId = Util::FileHelpers::SanitizeFileName(name);
		if (packId.empty())
			continue;
		const auto packRoot = Util::PathHelpers::GetUnifiedPackPath(packId);
		if (std::filesystem::exists(packRoot, ec)) {
			logger::info("[PostProcessingPresets] '{}' left in PostProcessing/: a preset named '{}' already exists", name, packId);
			continue;
		}

		auto document = Util::FileHelpers::ReadJsonFile(source, "post processing baseline");
		if (!document)
			continue;
		if (!WriteDocument(packRoot, ToBaselineDocument(std::move(*document))) ||
			!UnifiedPresetCatalog::EnsureBaselineManifest(packRoot, name)) {
			logger::warn("[PostProcessingPresets] Could not convert '{}' into a preset; it stays in PostProcessing/", name);
			continue;
		}

		auto migratedPath = source;
		migratedPath += kMigratedSuffix;
		std::filesystem::rename(source, migratedPath, ec);
		if (ec)
			logger::warn("[PostProcessingPresets] Converted '{}' but could not rename the original: {}", name, ec.message());
		logger::info("[PostProcessingPresets] Moved Post Processing preset '{}' to the Presets pack '{}'", name, packId);
	}
}

bool PostProcessingPresets::WriteBaseline(const std::filesystem::path& packRoot)
{
	auto& postProcessing = globals::features::postProcessing;
	if (!postProcessing.loaded)
		return false;

	json settings;
	{
		// The base settings, not whatever a scene layer has applied over them right now.
		SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
		postProcessing.SaveSettings(settings);
	}
	if (!WriteDocument(packRoot, ToBaselineDocument(std::move(settings))))
		return false;

	BaselineManager::GetSingleton()->Refresh();
	globals::state->ReapplySettingsLayers();
	return true;
}
