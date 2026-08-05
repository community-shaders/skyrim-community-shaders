#include "PresetManager.h"

#include "EffectManager.h"
#include "ENBExtender.h"
#include "SettingManager.h"
#include "Utils/FileSystem.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <Windows.h>

namespace
{
	// ShellExecute returns values > 32 on success (Win32).
	constexpr intptr_t kShellExecuteSuccessThreshold = 32;

	bool FileStartsWithKIEFX(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;

		std::string header(ENBExtender::kKIEFXMagicSize, '\0');
		file.read(header.data(), static_cast<std::streamsize>(header.size()));
		if (static_cast<size_t>(file.gcount()) < ENBExtender::kKIEFXMagicSize)
			return false;

		return ENBExtender::IsKIEFX(header);
	}
}

PresetManager& PresetManager::GetSingleton()
{
	static PresetManager instance;
	return instance;
}

std::string PresetManager::FormatPresetIdForLog(const std::string& id)
{
	return id.empty() ? "Legacy" : id;
}

std::filesystem::path PresetManager::GetPresetsRoot() const
{
	return Util::PathHelpers::GetEffects11PresetsPath();
}

std::filesystem::path PresetManager::GetPresetsRealPath() const
{
	if (!Util::PathHelpers::GetRootRealPath().empty())
		return Util::PathHelpers::GetEffects11PresetsRealPath();
	return GetPresetsRoot();
}

void PresetManager::EnsurePresetsFolderExists() const
{
	Util::FileHelpers::EnsureDirectoryExists(GetPresetsRealPath());
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

	if (HasLegacyInstall()) {
		activePresetId = kLegacyPresetId;
		return;
	}

	for (const auto& preset : presets) {
		if (!preset.isLegacy && preset.valid) {
			activePresetId = preset.id;
			return;
		}
	}

	activePresetId = kLegacyPresetId;
}

void PresetManager::DiscoverPresets()
{
	EnsurePresetsFolderExists();
	presets.clear();

	PresetInfo legacy;
	legacy.id = kLegacyPresetId;
	legacy.displayName = "Legacy (game root / Data)";
	legacy.isLegacy = true;
	legacy.valid = HasLegacyInstall();
	if (!legacy.valid)
		legacy.invalidReason = std::format("No {} + {} folder at game root or Data", kEnbSeriesIniName, kEnbSeriesDirName);
	else
		legacy.rootPath = GetLegacyENBSeriesIniPath().parent_path();
	presets.push_back(std::move(legacy));

	// Scan the real on-disk library (same path Open Folder uses). Under MO2, newly
	// added folders may not appear via the Data VFS path until a full refresh.
	const auto root = GetPresetsRealPath();
	std::error_code ec;
	if (std::filesystem::is_directory(root, ec)) {
		for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
			if (ec)
				break;
			if (!entry.is_directory())
				continue;

			PresetInfo info;
			info.id = entry.path().filename().string();
			info.displayName = info.id;
			info.rootPath = entry.path();
			info.isLegacy = false;
			info.valid = ValidateLibraryPreset(entry.path(), info.invalidReason);
			presets.push_back(std::move(info));
		}
	}

	// Keep Legacy first; sort library entries by display name.
	if (presets.size() > 1) {
		std::sort(presets.begin() + 1, presets.end(), [](const PresetInfo& a, const PresetInfo& b) {
			return a.displayName < b.displayName;
		});
	}

	discovered = true;
	EnsureDefaultSelection();

	logger::info("[Effects11] Discovered {} preset(s); active='{}'",
		presets.size(), FormatPresetIdForLog(activePresetId));
}

size_t PresetManager::GetValidLibraryPresetCount() const
{
	size_t count = 0;
	for (const auto& preset : presets) {
		if (!preset.isLegacy && preset.valid)
			++count;
	}
	return count;
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
	// EffectManager::Apply() unloads/recompiles ENB FX only — not CS shader cache.
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

bool PresetManager::OpenPresetsFolder() const
{
	// ShellExecute cannot open MO2 VFS paths. Use the real on-disk folder under the CS mod
	// (same pattern as Themes).
	EnsurePresetsFolderExists();
	const auto realPath = GetPresetsRealPath();

	const auto result = ShellExecuteW(nullptr, L"open", realPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<intptr_t>(result) > kShellExecuteSuccessThreshold;
}

bool PresetManager::ActivePresetUsesKIEFX() const
{
	auto& effectManager = EffectManager::GetSingleton();
	const Effect* coreEffects[] = {
		&effectManager.enbEffect,
		&effectManager.enbBloom,
		&effectManager.enbLens,
		&effectManager.enbAdaptation,
		&effectManager.enbEffectPostPass
	};

	const auto seriesPath = GetENBSeriesPath();
	for (const Effect* effect : coreEffects) {
		const auto path = seriesPath / effect->GetName();
		if (std::filesystem::exists(path) && FileStartsWithKIEFX(path))
			return true;
	}
	return false;
}

std::string PresetManager::GetActivePresetStatusSummary() const
{
	const auto* preset = FindPreset(activePresetId);
	const std::string name = preset ? preset->displayName : "Unknown";
	return std::format("{} — {}", name, GetENBSeriesPath().string());
}
