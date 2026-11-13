#pragma once

#include <filesystem>
#include <string>
#include <vector>

/** @brief Resolves the active ENBSeries root: the Legacy install, a library preset, or a unified pack's effects11/.
 *  Hotswap only redirects the paths it returns, so the Legacy install is never copied or modified. */
class PresetManager
{
public:
	/** @brief Empty string identifies the Legacy (root / Data) install. */
	static constexpr const char* kLegacyPresetId = "";

	/** @brief Prefix for unified-pack effects11 folders registered in the library. */
	static constexpr const char* kUnifiedPackIdPrefix = "pack:";

	static constexpr const char* kEnbSeriesDirName = "enbseries";
	static constexpr const char* kEnbSeriesIniName = "enbseries.ini";
	/** @brief Required FX file for a valid library preset (matches ENBEffect::GetName()). */
	static constexpr const char* kRequiredEffectFile = "enbeffect.fx";

	/** @brief One discovered preset, valid or not. */
	struct PresetInfo
	{
		std::string id;  ///< Empty for Legacy; folder name or pack:id for library
		std::string displayName;
		std::filesystem::path rootPath;  ///< Contains enbseries.ini + enbseries/
		bool valid = true;
		std::string invalidReason;
		bool isLegacy = false;
		bool isUnifiedPack = false;
	};

	static PresetManager& GetSingleton();

	/** @brief Active preset's enbseries directory (library or Legacy). */
	std::filesystem::path GetENBSeriesPath() const;
	/** @brief Active preset's enbseries.ini path (library or Legacy). */
	std::filesystem::path GetENBSeriesIniPath() const;

	/** @brief Rescans Legacy, library and unified pack folders and repairs an invalid active selection. */
	void DiscoverPresets();
	/**
	 * @brief Lists Legacy first (invalid when not installed), then library presets and, when includeUnifiedPacks,
	 *        unified packs' effects11 folders, sorted by name. Leaves the discovered list and the selection alone.
	 */
	std::vector<PresetInfo> ScanPresets(bool includeUnifiedPacks) const;
	/** @brief Every preset found by the last DiscoverPresets. */
	const std::vector<PresetInfo>& GetPresets() const { return presets; }

	/** @brief The selected preset's id; kLegacyPresetId for Legacy. */
	const std::string& GetActivePresetId() const { return activePresetId; }

	/** @brief Selects the first valid library or unified pack preset, falling back to Legacy when none exists. */
	void SelectDefaultPreset();

	/** @brief Selects a discovered preset without reloading FX.
	 *  @return False if the id is unknown or invalid. */
	bool SetActivePreset(const std::string& id);

	/** @brief Whether an enbseries.ini and enbseries/ exist under Data or the game root. */
	bool HasLegacyInstall() const;
	/** @brief Whether the Legacy install is the active preset. */
	bool IsLegacyActive() const { return activePresetId == kLegacyPresetId; }
	/** @brief Absolute Legacy enbseries.ini path. */
	std::filesystem::path GetLegacyENBSeriesIniPath() const;

	/** @brief Hotswap: optionally saves the current preset, then reloads settings and weather and recompiles
	 *         ENB FX for id. Does not clear the Community Shaders shader cache. */
	bool SwitchPreset(const std::string& id, bool saveCurrent = true);

	/** @brief Reloads settings and weather and recompiles ENB FX for the active preset. */
	void ReloadActive();

	/** @brief Library id for a unified pack's effects11 folder. */
	static std::string MakeUnifiedPackPresetId(const std::string& packId);

	/** @brief Whether presetRoot has enbseries.ini and enbseries/enbeffect.fx; outReason says what is missing. */
	static bool ValidateLibraryPreset(const std::filesystem::path& presetRoot, std::string& outReason);

	/** @brief Folder that currently holds the active enbseries.ini + enbseries/ (library, pack, or Legacy). */
	std::filesystem::path GetActivePresetRoot() const;

	/** @brief Whether the active selection has a valid ENBSeries layout that can be exported. */
	bool CanExportActivePreset() const;

	/**
	 * @brief Saves the live ENB settings (optional) and copies enbseries.ini + enbseries/ into destRoot.
	 *  Only those ENB files are copied, never the whole Legacy game/Data root. No-ops when source == dest.
	 */
	bool ExportActivePresetTo(const std::filesystem::path& destRoot, bool saveCurrent = true);

private:
	/** @brief Whether the Legacy install lives under Data rather than the game root. */
	bool UseDataFolder() const;
	/** @brief Absolute Legacy enbseries directory. */
	std::filesystem::path GetLegacyENBSeriesPath() const;

	/** @brief Library preset root when a valid non-Legacy preset is active; empty otherwise. */
	std::filesystem::path GetActiveLibraryRoot() const;

	/** @brief Whether the ini exists and the series folder is a directory. */
	static bool IsValidEnbSeriesLayout(const std::filesystem::path& iniPath, const std::filesystem::path& seriesDir);
	/** @brief The id, or "Legacy" for the empty Legacy id. */
	static std::string FormatPresetIdForLog(const std::string& id);

	/** @brief The discovered preset with this id, or null. */
	const PresetInfo* FindPreset(const std::string& id) const;
	/** @brief Keeps a valid selection, else applies SelectDefaultPreset. */
	void EnsureDefaultSelection();
	/** @brief Adds each preset folder under a CommunityShaders subfolder, across the VFS and real mod roots. */
	static void ScanLibraryDirectory(const std::filesystem::path& relativePath, bool unifiedPacks, std::vector<PresetInfo>& out);

	std::vector<PresetInfo> presets;
	std::string activePresetId = kLegacyPresetId;
	bool discovered = false;
};
