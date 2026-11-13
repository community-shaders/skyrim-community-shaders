#include "PresetManager.h"

#include "EffectManager.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "SettingManager.h"
#include "Utils/FileSystem.h"

#include <algorithm>
#include <format>

PresetManager& PresetManager::GetSingleton()
{
	static PresetManager instance;
	return instance;
}

std::string PresetManager::FormatPresetIdForLog(const std::string& id)
{
	return id.empty() ? "Legacy" : id;
}

std::string PresetManager::MakeUnifiedPackPresetId(const std::string& packId)
{
	return std::string(kUnifiedPackIdPrefix) + packId;
}

bool PresetManager::UseDataFolder() const
{
	return IsValidEnbSeriesLayout(
		std::filesystem::path("Data") / kEnbSeriesIniName,
		std::filesystem::path("Data") / kEnbSeriesDirName);
}

bool PresetManager::IsValidEnbSeriesLayout(const std::filesystem::path& iniPath, const std::filesystem::path& seriesDir)
{
	return std::filesystem::exists(iniPath) && std::filesystem::is_directory(seriesDir);
}

bool PresetManager::HasLegacyInstall() const
{
	if (UseDataFolder())
		return true;
	return IsValidEnbSeriesLayout(kEnbSeriesIniName, kEnbSeriesDirName);
}

std::filesystem::path PresetManager::GetLegacyENBSeriesPath() const
{
	if (UseDataFolder())
		return std::filesystem::absolute(std::filesystem::path("Data") / kEnbSeriesDirName);
	return std::filesystem::absolute(kEnbSeriesDirName);
}

std::filesystem::path PresetManager::GetLegacyENBSeriesIniPath() const
{
	if (UseDataFolder())
		return std::filesystem::absolute(std::filesystem::path("Data") / kEnbSeriesIniName);
	return std::filesystem::absolute(kEnbSeriesIniName);
}

bool PresetManager::ValidateLibraryPreset(const std::filesystem::path& presetRoot, std::string& outReason)
{
	const auto iniPath = presetRoot / kEnbSeriesIniName;
	const auto seriesPath = presetRoot / kEnbSeriesDirName;

	if (!std::filesystem::exists(iniPath)) {
		outReason = std::format("Missing {}", kEnbSeriesIniName);
		return false;
	}
	if (!std::filesystem::is_directory(seriesPath)) {
		outReason = std::format("Missing {} folder", kEnbSeriesDirName);
		return false;
	}
	if (!std::filesystem::exists(seriesPath / kRequiredEffectFile)) {
		outReason = std::format("Missing required {}", kRequiredEffectFile);
		return false;
	}
	return true;
}

const PresetManager::PresetInfo* PresetManager::FindPreset(const std::string& id) const
{
	for (const auto& preset : presets) {
		if (preset.id == id)
			return &preset;
	}
	return nullptr;
}

void PresetManager::EnsureDefaultSelection()
{
	if (const auto* current = FindPreset(activePresetId); current && current->valid)
		return;
	SelectDefaultPreset();
}

void PresetManager::SelectDefaultPreset()
{
	for (const auto& preset : presets) {
		if (!preset.isLegacy && preset.valid) {
			activePresetId = preset.id;
			return;
		}
	}

	activePresetId = kLegacyPresetId;
}

void PresetManager::ScanLibraryDirectory(const std::filesystem::path& relativePath, bool unifiedPacks, std::vector<PresetInfo>& out)
{
	for (const auto& folder : Util::PathHelpers::ListCommunityShaderEntries(relativePath, true)) {
		const auto packName = Util::PathHelpers::ToUtf8(folder.filename());
		auto libraryRoot = folder;
		auto displayName = packName;

		if (unifiedPacks) {
			const auto manifest = UnifiedPresetCatalog::ReadPackManifest(folder);
			libraryRoot = UnifiedPresetCatalog::ResolveEffects11Root(folder, manifest);
			if (libraryRoot.empty())
				continue;
			if (const auto name = manifest.find(PackManifest::kNameKey); name != manifest.end() && name->is_string())
				displayName = name->get<std::string>();
		}

		PresetInfo info;
		info.id = unifiedPacks ? MakeUnifiedPackPresetId(packName) : packName;
		info.displayName = std::move(displayName);
		info.rootPath = libraryRoot;
		info.isLegacy = false;
		info.isUnifiedPack = unifiedPacks;
		info.valid = ValidateLibraryPreset(libraryRoot, info.invalidReason);
		out.push_back(std::move(info));
	}
}

void PresetManager::DiscoverPresets()
{
	presets = ScanPresets(true);
	discovered = true;
	EnsureDefaultSelection();

	logger::info("[Effects11] Discovered {} preset(s); active='{}'",
		presets.size(), FormatPresetIdForLog(activePresetId));
}

std::vector<PresetManager::PresetInfo> PresetManager::ScanPresets(bool includeUnifiedPacks) const
{
	Util::FileHelpers::EnsureDirectoryExists(Util::PathHelpers::GetEffects11PresetsRealPath());
	Util::FileHelpers::EnsureDirectoryExists(Util::PathHelpers::GetUnifiedPresetsRealPath());
	std::vector<PresetInfo> found;

	PresetInfo legacy;
	legacy.id = kLegacyPresetId;
	legacy.displayName = "Legacy (game root / Data)";
	legacy.isLegacy = true;
	legacy.valid = HasLegacyInstall();
	if (!legacy.valid)
		legacy.invalidReason = std::format("No {} + {} folder at game root or Data", kEnbSeriesIniName, kEnbSeriesDirName);
	else
		legacy.rootPath = GetLegacyENBSeriesIniPath().parent_path();
	found.push_back(std::move(legacy));

	ScanLibraryDirectory(Util::PathHelpers::kEffects11PresetsSubdir, false, found);
	if (includeUnifiedPacks)
		ScanLibraryDirectory(Util::PathHelpers::kUnifiedPresetsSubdir, true, found);

	// Keep Legacy first; sort library entries by display name.
	std::sort(found.begin() + 1, found.end(), [](const PresetInfo& a, const PresetInfo& b) {
		return a.displayName < b.displayName;
	});
	return found;
}

std::filesystem::path PresetManager::GetActiveLibraryRoot() const
{
	if (IsLegacyActive())
		return {};

	if (const auto* preset = FindPreset(activePresetId); preset && preset->valid && !preset->isLegacy)
		return preset->rootPath;

	return {};
}

std::filesystem::path PresetManager::GetENBSeriesPath() const
{
	if (const auto libraryRoot = GetActiveLibraryRoot(); !libraryRoot.empty())
		return std::filesystem::absolute(libraryRoot / kEnbSeriesDirName);
	return GetLegacyENBSeriesPath();
}

std::filesystem::path PresetManager::GetENBSeriesIniPath() const
{
	if (const auto libraryRoot = GetActiveLibraryRoot(); !libraryRoot.empty())
		return std::filesystem::absolute(libraryRoot / kEnbSeriesIniName);
	return GetLegacyENBSeriesIniPath();
}

bool PresetManager::SetActivePreset(const std::string& id)
{
	if (!discovered)
		DiscoverPresets();

	const auto* preset = FindPreset(id);
	if (!preset) {
		logger::warn("[Effects11] Unknown preset id '{}'", FormatPresetIdForLog(id));
		return false;
	}
	if (!preset->valid) {
		logger::warn("[Effects11] Cannot activate invalid preset '{}': {}", preset->displayName, preset->invalidReason);
		return false;
	}

	activePresetId = id;
	logger::info("[Effects11] Active preset set to '{}'", FormatPresetIdForLog(id));
	return true;
}

void PresetManager::ReloadActive()
{
	// SettingManager::Load() reinitializes WeatherManager via ReloadAllWeatherSettings().
	// EffectManager::Apply() unloads/recompiles ENB FX only, not the CS shader cache.
	SettingManager::GetSingleton().Load();
	EffectManager::GetSingleton().Apply();
}

bool PresetManager::SwitchPreset(const std::string& id, bool saveCurrent)
{
	if (!discovered)
		DiscoverPresets();

	if (id == activePresetId) {
		if (const auto* current = FindPreset(activePresetId); current && current->valid)
			return true;
	}

	const auto* target = FindPreset(id);
	if (!target || !target->valid)
		return false;

	auto& settingManager = SettingManager::GetSingleton();
	auto& effectManager = EffectManager::GetSingleton();

	if (saveCurrent) {
		if (const auto* current = FindPreset(activePresetId); current && current->valid) {
			settingManager.Save();
			effectManager.Save();
		}
	}

	if (!SetActivePreset(id))
		return false;

	ReloadActive();
	return true;
}

std::filesystem::path PresetManager::GetActivePresetRoot() const
{
	return GetENBSeriesIniPath().parent_path();
}

bool PresetManager::CanExportActivePreset() const
{
	std::string reason;
	return ValidateLibraryPreset(GetActivePresetRoot(), reason);
}

bool PresetManager::ExportActivePresetTo(const std::filesystem::path& destRoot, bool saveCurrent)
{
	if (destRoot.empty()) {
		logger::error("[Effects11] Export failed: destination root is empty");
		return false;
	}

	if (!discovered)
		DiscoverPresets();

	const auto* current = FindPreset(activePresetId);
	if (!current || !current->valid) {
		logger::error("[Effects11] Export failed: active preset '{}' is missing or invalid",
			FormatPresetIdForLog(activePresetId));
		return false;
	}

	if (saveCurrent) {
		SettingManager::GetSingleton().Save();
		EffectManager::GetSingleton().Save();
	}

	const auto sourceRoot = GetActivePresetRoot();
	std::string sourceReason;
	if (!ValidateLibraryPreset(sourceRoot, sourceReason)) {
		logger::error("[Effects11] Export failed: active preset root '{}' is invalid ({})",
			Util::PathHelpers::ToUtf8(sourceRoot), sourceReason);
		return false;
	}

	std::error_code ec;
	if (std::filesystem::equivalent(sourceRoot, destRoot, ec)) {
		logger::info("[Effects11] Export skipped copy; '{}' is already the active preset root", Util::PathHelpers::ToUtf8(destRoot));
		return true;
	}

	Util::FileHelpers::EnsureDirectoryExists(destRoot);

	const auto srcIni = sourceRoot / kEnbSeriesIniName;
	const auto srcSeries = sourceRoot / kEnbSeriesDirName;
	const auto destIni = destRoot / kEnbSeriesIniName;
	const auto destSeries = destRoot / kEnbSeriesDirName;

	std::filesystem::remove(destIni, ec);
	std::filesystem::remove_all(destSeries, ec);

	std::filesystem::copy_file(srcIni, destIni, std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		logger::error("[Effects11] Export failed copying '{}': {}", Util::PathHelpers::ToUtf8(srcIni), ec.message());
		return false;
	}

	std::filesystem::copy(srcSeries, destSeries,
		std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		logger::error("[Effects11] Export failed copying '{}': {}", Util::PathHelpers::ToUtf8(srcSeries), ec.message());
		return false;
	}

	// Optional companion file some ENB packs ship next to enbseries.ini.
	const auto srcLocal = sourceRoot / "enblocal.ini";
	if (std::filesystem::is_regular_file(srcLocal, ec)) {
		std::filesystem::copy_file(srcLocal, destRoot / "enblocal.ini",
			std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
			logger::warn("[Effects11] Export could not copy enblocal.ini: {}", ec.message());
	}

	std::string destReason;
	if (!ValidateLibraryPreset(destRoot, destReason)) {
		logger::error("[Effects11] Export wrote an invalid layout at '{}': {}", Util::PathHelpers::ToUtf8(destRoot), destReason);
		return false;
	}

	logger::info("[Effects11] Exported active preset '{}' to '{}'",
		FormatPresetIdForLog(activePresetId), Util::PathHelpers::ToUtf8(destRoot));
	return true;
}
