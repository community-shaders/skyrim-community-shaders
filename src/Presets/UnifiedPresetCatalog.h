#pragma once

#include <d3d11.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

#include "Features/Effects11/SceneHandoff.h"
#include "PackManifest.h"
#include "PostProcessingMode.h"
#include "PresetCompatibility.h"
#include "Utils/UI.h"

struct Feature;

/** @brief Discovers unified preset packs (Effects 11 and CS Presets payloads) plus orphan Effects 11 presets. */
class UnifiedPresetCatalog
{
public:
	/** @brief Where a pack was found: a Presets pack folder, a standalone Effects 11 preset, or the game root / Data install. */
	enum class SourceKind
	{
		UnifiedPack,
		Effects11Orphan,
		Effects11Legacy
	};

	/** @brief Pipeline a pack targets, declared by the manifest's "type" field. */
	enum class PresetType
	{
		CS,
		E11,
		Baseline
	};

	static constexpr const char* kPresetTypeKey = "type";
	/** @brief Manifest object of feature short names → disabled-at-boot (same shape as Settings "Disable at Boot"). */
	static constexpr const char* kDisableAtBootKey = "disableAtBoot";
	/** @brief Manifest object of feature short names → handed from Effects 11 back to CS scene settings (E11Handoff). */
	static constexpr const char* kSceneControlKey = "sceneControl";
	/** @brief Default relative folder inside a pack that holds enbseries.ini + enbseries/. */
	static constexpr const char* kEffects11PackSubdir = "effects11";
	/** @brief Folder inside a pack holding baseline feature overwrites, one `<FeatureShortName>.json` per feature. */
	static constexpr const char* kBaselineSubdir = "Baseline";

	/** @brief The manifest spelling of a preset type. */
	static const char* GetPresetTypeName(PresetType type);

	/** @brief A discovered pack's manifest data, backend payloads and lazily loaded artwork. */
	struct PackInfo : PackManifest
	{
		std::string id;
		/// csVersion, requiredFeatures and requiredPlugins checked against this session, refreshed by each Discover.
		PresetCompatibility::Warning compat;

		bool hasEffects11 = false;
		bool hasCSPresets = false;
		bool hasBaseline = false;
		bool hasFormEdits = false;  ///< Ships CS Editor form edits under Forms/; applied as CS content.
		/// Feature short names the pack's Baseline folder sets, sorted; empty without a baseline payload.
		std::vector<std::string> baselineFeatures;
		/// Feature short names → disabled at boot when this baseline pack is enabled (manifest disableAtBoot).
		std::unordered_map<std::string, bool> disableAtBoot;
		/// Declared type; groups the pack, while the has* payload flags still decide what Apply loads.
		std::optional<PresetType> type;
		bool valid = true;
		std::string invalidReason;

		SourceKind source = SourceKind::UnifiedPack;
		std::filesystem::path rootPath;       ///< Pack folder or library root
		std::filesystem::path effects11Root;  ///< Folder with enbseries.ini + enbseries/

		std::filesystem::path logoPath;
		std::filesystem::path coverPath;
		std::vector<std::filesystem::path> screenshotPaths;

		winrt::com_ptr<ID3D11ShaderResourceView> logoSRV;
		winrt::com_ptr<ID3D11ShaderResourceView> coverSRV;
		std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> screenshotSRVs;
		bool artworkLoaded = false;

		/** @brief Grouped as E11: the declared type, else whether an Effects 11 payload was found. */
		bool IsE11() const { return type ? *type == PresetType::E11 : hasEffects11; }
		/** @brief Grouped as CS: the declared type, else whether scene files or form edits were found. */
		bool IsCS() const { return type ? *type == PresetType::CS : hasCSPresets || hasFormEdits; }
		/** @brief Grouped as Baseline: the declared type, else whether a Baseline folder was found. */
		bool IsBaseline() const { return type ? *type == PresetType::Baseline : hasBaseline; }
		/** @brief Grouped under the given type. */
		bool IsType(PresetType presetType) const
		{
			switch (presetType) {
			case PresetType::E11:
				return IsE11();
			case PresetType::Baseline:
				return IsBaseline();
			default:
				return IsCS();
			}
		}
		/** @brief Whether a CS or E11 export may write into the pack: false when it is grouped only under the other one. */
		bool AcceptsExport(PresetType exportType) const
		{
			// Base settings fit any pack: CS and E11 presets carry a Baseline folder of their own.
			if (exportType == PresetType::Baseline)
				return true;
			const auto otherType = exportType == PresetType::E11 ? PresetType::CS : PresetType::E11;
			return IsType(exportType) || !IsType(otherType);
		}
	};

	static UnifiedPresetCatalog& GetSingleton();

	/** @brief Rescans every pack and orphan, sorted by name, releasing loaded artwork and reloading the active id. */
	void Discover();
	/** @brief Packs from the last Discover. */
	const std::vector<PackInfo>& GetPacks() const { return packs; }

	/** @brief The applied pack's id, empty when none. */
	const std::string& GetActivePackId() const { return activePackId; }
	/** @brief Sets and persists the active pack id. */
	void SetActivePackId(const std::string& id);

	/** @brief The Effects 11 preset the active pack selects, or empty when no pack is active. */
	std::string GetActiveEffects11PresetId() const;

	/** @brief The pipeline the last applied or disabled pack chose, if any. */
	std::optional<PostProcessingMode::Mode> GetActivePipeline() const { return activePipeline; }
	/** @brief Bumped each time a pack chooses the pipeline. The user config records the revision it was
	 *  saved against, so a pack applied after the last save decides the pipeline at the next start. */
	uint32_t GetPipelineRevision() const { return pipelineRevision; }

	/** @brief Folder of the active unified pack; empty when none is active or the active preset is an orphan. */
	std::filesystem::path GetActivePackRoot() const;

	/** @brief Resolves a relative path against the game directory first, then the active pack root.
	 *  @return The first existing candidate, or the path unchanged when neither exists. */
	std::filesystem::path ResolveActivePackPath(const std::filesystem::path& path) const;

	/** @brief The pack with this id, or null. */
	PackInfo* FindPack(const std::string& id);
	const PackInfo* FindPack(const std::string& id) const;

	/** @brief Indices of packs of the given type (any when null) matching a case-insensitive name/author/description/tag search. */
	std::vector<size_t> Query(std::optional<PresetType> typeFilter, const std::string& search) const;

	/** @brief Loads a pack's logo, cover and screenshots once; the cover stands in for a missing logo. */
	bool EnsureArtwork(PackInfo& pack);
	/** @brief Drops every pack's artwork textures. */
	void ReleaseAllArtwork();

	/** @brief Applies Effects11, swaps the Scene Manager overwrite layer to this pack's CS Presets files, and
	 *  enables its Baseline overwrites. A Baseline-only pack leaves the active pack and scene layer untouched. */
	bool ApplyPack(const std::string& id, bool saveEffects11Current = true);

	/** @brief Turns the active pack off: an Effects 11 pack switches the pipeline to Vanilla,
	 *  and the scene layer and form edits fall back to the user's own. */
	void DisableActivePack();

	/** @brief Ids of packs whose Baseline overwrites are enabled, in layering order (a later pack wins conflicts). */
	const std::vector<std::string>& GetBaselinePackIds() const { return baselinePackIds; }
	/** @brief Whether a pack's Baseline overwrites are enabled. */
	bool IsBaselineEnabled(const std::string& id) const;
	/** @brief Reloads the layers a pack that is active or has its Baseline enabled supplies, so freshly exported
	 *  files take effect. The pipeline and the Baseline order stay as they are. */
	void ReapplyIfInUse(const std::string& id);
	/** @brief Asks, once for every pack enabled since the last answer, whether Baseline packs that set settings
	 *  the user changed should replace those edits. Call every frame the menu or the CS editor is open. */
	void DrawBaselineOverlapPrompt();

	/** @brief Stops applying a pack's Baseline overwrites; its features fall back to the layers beneath,
	 *  with the user's edits kept on top.
	 *  @return False when the pack was not enabled. */
	bool RemoveBaseline(const std::string& id);

	/** @brief Moves an enabled Baseline pack one step in the layering order and reapplies the layers.
	 *  @param raise True moves it later, so it wins conflicts against the pack it passes; false moves it earlier.
	 *  @return False when the pack is not enabled, is already at that end, or the new order could not be saved. */
	bool MoveBaseline(const std::string& id, bool raise);

	/** @brief Sets the Effects 11 handoff flags from the active pack's manifest sceneControl, overlaid with the
	 *  user's choices for that pack; no active pack hands nothing off. */
	void LoadSceneControl() const;
	/** @brief Sets a feature's Effects 11 handoff flag and, with a pack active, keeps it as the user's choice for
	 *  that pack. The pack's manifest is never written. */
	void SetSceneControl(E11Handoff::Feature feature, bool handedOff);

	/** @brief SettingsUser.json section holding the active pack state, written whenever it changes. */
	static constexpr const char* kUserSettingsKey = "Presets";
	/** @brief The active pack state as stored under kUserSettingsKey; State::Save writes it with the rest. */
	nlohmann::json SerializeActiveState() const;

	/** @brief Writes a starter manifest for a pack folder that has none, so an exported Baseline shows up as a preset.
	 *  @return True when a manifest exists afterwards. */
	static bool EnsureBaselineManifest(const std::filesystem::path& packRoot, const std::string& displayName);

	/** @brief Creates the presets root if needed and opens its real path in Explorer. */
	bool OpenPresetsFolder() const;
	/** @brief Opens a pack's folder in Explorer, preferring its physical copy over the MO2 VFS path. */
	bool OpenPackFolder(const std::string& id) const;

	/** @brief A pack's manifest file: `<PackId>.json`, else the legacy `preset.json` when only that exists. */
	static std::filesystem::path GetPackManifestPath(const std::filesystem::path& packRoot);

	/** @brief Parses a pack's manifest.
	 *  @param outError Receives the parse error when the manifest exists but is invalid.
	 *  @return The manifest object, or an empty object when absent or invalid. */
	static nlohmann::json ReadPackManifest(const std::filesystem::path& packRoot, std::string* outError = nullptr);

	/** @brief Folder holding a pack's enbseries.ini + enbseries/: the manifest's `effects11.path`, then `effects11/`,
	 *  then the pack root. Empty when none validates or the manifest sets `backends.effects11` to false. */
	static std::filesystem::path ResolveEffects11Root(const std::filesystem::path& packRoot, const nlohmann::json& manifest);

private:
	UnifiedPresetCatalog() { LoadActiveState(); }

	/** @brief Reads the persisted active pack state from SettingsUser.json, clearing it when missing or bad. */
	void LoadActiveState();
	/** @brief Writes the active pack state into its SettingsUser.json section, leaving the rest of the file alone.
	 *  @return False when the user config could not be read or written. */
	bool SaveActiveState() const;

	/** @brief Enables a pack's Baseline overwrites as the last layer and reapplies them to the loaded features. */
	void EnableBaseline(const PackInfo& pack);
	/** @brief Adds a pack whose Baseline sets settings the user changed to the pending overlap prompt. */
	void QueueBaselineOverlap(const std::string& packId, const std::vector<Feature*>& features);

	/** @brief Adds each folder under Presets as a pack, read from its manifest. */
	void DiscoverUnifiedPacks();
	/** @brief Adds the Effects 11 install at the game root or Data folder, when present, and the library presets
	 *  no unified pack of the same name already covers. */
	void DiscoverEffects11Presets();
	/** @brief Switches to the pipeline a pack chose, recording it as the pack layer's choice; the caller persists.
	 *  @return False, recording nothing, when the pipeline cannot run. */
	bool UsePackPipeline(PostProcessingMode::Mode mode);

	/** @brief Decodes an image file into an SRV; false when missing or undecodable. */
	static bool LoadTextureSRV(const std::filesystem::path& path, winrt::com_ptr<ID3D11ShaderResourceView>& outSRV);

	std::vector<PackInfo> packs;
	std::string activePackId;
	std::vector<std::string> baselinePackIds;
	std::optional<PostProcessingMode::Mode> activePipeline;
	uint32_t pipelineRevision = 0;
	/// Pack id → the user's Effects 11 handoff choices for it, over the pack's own sceneControl.
	nlohmann::json sceneControlOverrides = nlohmann::json::object();

	/// Packs, and the feature names they overlap, waiting on the overlap prompt.
	std::vector<std::string> overlapPackIds;
	std::vector<std::string> overlapFeatureNames;
	Util::ConfirmationPopup overlapPrompt;
};
