#pragma once

#include <filesystem>
#include <string>
#include <vector>

/**
 * @brief Resolves the active ENBSeries preset root for Effects11.
 *
 * Supports a classic Legacy install (game-root or Data\\enbseries) and a multi-preset
 * library under Data\\SKSE\\Plugins\\CommunityShaders\\Effects11\\Presets\\<Name>\\
 * (same CommunityShaders plugin tree as Themes / Overrides). Hotswap redirects
 * GetENBSeriesPath() / GetENBSeriesIniPath() without copying or modifying the Legacy install.
 */
class PresetManager
{
public:
	/** Empty string identifies the Legacy (root / Data) install. */
	static constexpr const char* kLegacyPresetId = "";

	/** Display / docs path (under Data via MO2 VFS). */
	static constexpr const char* kPresetsRootRelative = "Data\\SKSE\\Plugins\\CommunityShaders\\Effects11\\Presets";
	static constexpr const char* kEnbSeriesDirName = "enbseries";
	static constexpr const char* kEnbSeriesIniName = "enbseries.ini";
	/** Required FX file for a valid library preset (matches ENBEffect::GetName()). */
	static constexpr const char* kRequiredEffectFile = "enbeffect.fx";

	struct PresetInfo
	{
		std::string id;                  ///< Empty for Legacy; folder name for library presets
		std::string displayName;
		std::filesystem::path rootPath;  ///< Contains enbseries.ini + enbseries/
		bool valid = true;
		std::string invalidReason;
		bool isLegacy = false;
	};

	static PresetManager& GetSingleton();

	/** @return In-game / VFS path to the presets library (Data\\...\\Effects11\\Presets). */
	std::filesystem::path GetPresetsRoot() const;

	/** @return On-disk path under the CS mod root (for Explorer and create_directories). */
	std::filesystem::path GetPresetsRealPath() const;

	/** Ensure the on-disk presets library folder exists (no-op if already present). */
	void EnsurePresetsFolderExists() const;

	/** @return Active preset's enbseries directory (library or Legacy). */
	std::filesystem::path GetENBSeriesPath() const;
	/** @return Active preset's enbseries.ini path (library or Legacy). */
	std::filesystem::path GetENBSeriesIniPath() const;

	/** Rescan Legacy + library folders and repair an invalid active selection. */
	void DiscoverPresets();
	const std::vector<PresetInfo>& GetPresets() const { return presets; }

	/** @return Count of valid non-Legacy library presets. */
	size_t GetValidLibraryPresetCount() const;

	const std::string& GetActivePresetId() const { return activePresetId; }

	/**
	 * @brief Selects a discovered preset without reloading FX.
	 * @param id Legacy id or library folder name
	 * @return false if unknown or invalid
	 */
	bool SetActivePreset(const std::string& id);

	bool HasLegacyInstall() const;
	bool IsLegacyActive() const { return activePresetId == kLegacyPresetId; }

	/**
	 * @brief Hotswap: optionally save current, set active id, reload settings/weather, recompile ENB FX.
	 * Does not clear Community Shaders shader cache.
	 */
	bool SwitchPreset(const std::string& id, bool saveCurrent = true);

	/** Reload settings/weather and recompile ENB FX for the current active preset. */
	void ReloadActive();

	/** Ensure the presets root exists and open it in Explorer (real path under MO2). */
	bool OpenPresetsFolder() const;

	/** @return true if any core FX in the active enbseries path uses KIEFX encryption. */
	bool ActivePresetUsesKIEFX() const;

	/** Human-readable "DisplayName — enbseries path" for the status line. */
	std::string GetActivePresetStatusSummary() const;

private:
	bool UseDataFolder() const;
	std::filesystem::path GetLegacyENBSeriesPath() const;
	std::filesystem::path GetLegacyENBSeriesIniPath() const;

	/** @return Library preset root when a valid non-Legacy preset is active; empty otherwise. */
	std::filesystem::path GetActiveLibraryRoot() const;

	static bool ValidateLibraryPreset(const std::filesystem::path& presetRoot, std::string& outReason);
	static bool IsValidEnbSeriesLayout(const std::filesystem::path& iniPath, const std::filesystem::path& seriesDir);
	static std::string FormatPresetIdForLog(const std::string& id);

	const PresetInfo* FindPreset(const std::string& id) const;
	void EnsureDefaultSelection();

	std::vector<PresetInfo> presets;
	std::string activePresetId = kLegacyPresetId;
	bool discovered = false;
};
