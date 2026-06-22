#pragma once
#include "../Features/InverseSquareLighting/Common.h"
#include "LightPicker.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <set>
#include <string_view>

namespace RE
{
	class BSLight;
}

struct LightEditor
{
	bool disableInvSqLights = false;
	bool disableRegularLights = false;
	bool shadowsOnly = false;

	/** @brief Draw the light editor ImGui settings panel. */
	void DrawSettings();

	/** @brief Gather all active scene lights into the internal list for display and editing. */
	void GatherLights();

	/** @brief Reset all light overrides to their original values. */
	void ResetOverrides();

	/**
	 * @brief Apply editor overrides to a specific light during rendering.
	 * @param niLight     The NiLight scene node to potentially override.
	 * @param runtimeData The runtime light data extension to modify.
	 * @return True if the light should be rendered, false if it should be suppressed.
	 */
	bool ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const;

private:
	struct LightInfo
	{
		bool isSelected = false;
		uint32_t id = 0;
		void* ptr = nullptr;
		uint32_t index = 0;
		std::string name;
		bool isRef = false;
		bool isAttached = false;
		bool isOther = false;
		bool isSpotlight = false;
		bool hasPosition = false;
		RE::NiPoint3 position;

		bool operator==(const LightInfo& other) const noexcept
		{
			return id == other.id && index == other.index;
		}
	};

	struct LightDisplayInfo
	{
		std::string ownerEditorId;
		RE::FormID baseObjectFormId = 0;
		std::string ownerLastEditedBy;
		RE::FormID cellFormId = 0;
		std::string cellEditorId;
		RE::FormID lighFormId = 0;
		std::string lighEditorId;
		RE::NiPoint3 pos = {};
	};

	struct LightSettings
	{
		stl::enumeration<ISLCommon::TES_LIGHT_FLAGS_EXT, uint32_t> tesFlags;
		ISLCommon::RuntimeLightDataExt data = {};
		RE::NiPoint3 pos = {};
	};

	bool extendedLogMode = false;
	bool saveColorToLP = false;
	bool useExternalEmittance = false;
	bool lpMatchFound = false;
	bool lpInWhitelist = false;
	bool lpInBlacklist = false;
	bool deleteConfirmPopupRequested = false;
	std::set<std::string> lpFlagSet;
	std::set<std::string> originalLpFlagSet;
	std::string externalEmittanceEdid;
	int32_t waitFrames = 0;
	uint32_t totalLightCount = 0;
	uint32_t activeShadowLightCount = 0;

	enum class FilterOption
	{
		RefLights,
		AttachedLights,
		OtherLights,
		Count
	};

	enum class SortOption
	{
		None,
		Distance,
		FormID,
		EditorID,
		Count
	};

	FilterOption filterOption = FilterOption::RefLights;
	SortOption sortOption = SortOption::Distance;

	std::vector<LightInfo> lights = {};
	std::unordered_map<RE::TESObjectREFR*, uint32_t> lightsAttached = {};

	LightInfo selected = {};
	LightInfo previous = {};
	LightInfo savedSelection = {};
	LightInfo comboHoveredLight = {};

	RE::NiPointer<RE::NiLight> hoverFlashNiLight;
	float hoverFlashOriginalFade = 0.f;
	bool hoverFlashVisible = true;
	double hoverFlashLastToggle = 0.0;

	LightDisplayInfo displayInfo = {};
	LightSettings original = {};
	LightSettings current = {};

	struct LPLightInfo
	{
		std::string configPath;
		std::string lightEDID;
		std::string ownerModelPath;
		std::string ownerEditorId;
		bool isLPLight = false;
	};

	LPLightInfo lpInfo;
	RE::NiPointer<RE::NiLight> activeNiLight;
	RE::NiPointer<RE::BSLight> activeBsLight;
	RE::TESObjectREFR* activeRefr = nullptr;
	RE::TESObjectLIGH* activeLigh = nullptr;
	bool activeIsRef = false;

	// External-emittance color preview (CS-driven, selected bulb only): the game only drives emittance
	// color for refs in the cell's emittance maps, so we track the source region's live color ourselves.
	RE::TESForm* activeEmittanceSource = nullptr;
	bool emittanceColorActive = false;  // gates ApplyOverrides to the emittance color below
	RE::NiColor emittanceColor{};

	// Emittance state at selection time, so Reset reverts a changed source (it lives outside LightSettings).
	RE::TESForm* originalEmittanceSource = nullptr;
	std::string originalExternalEmittanceEdid;
	bool originalUseExternalEmittance = false;

	// Deferred 3D rebuild after a light-flag edit: the engine's async despawn/respawn can complete out of
	// order if issued back-to-back, so Disable now and Enable a few frames later (coalescing repeat edits).
	RE::ObjectRefHandle pendingRefreshRefr;
	int32_t pendingRefreshFrames = 0;
	static constexpr int32_t kRefreshEnableDelay = 3;

	float shadowDepthBias = 0.0f;
	float originalShadowDepthBias = 0.0f;
	float cachedFadeBeforeToggle = 0.0f;

	/** @brief Sorts the gathered light list by the active sort option. */
	void SortLights();
	/** @brief Restores the active light to its snapshotted original state. */
	void RestoreOriginal();
	/** @brief Writes shadowDepthBias to the active shadow light's runtime data. */
	void ApplyShadowDepthBias();
	/** @brief Disables refr now and schedules its Enable kRefreshEnableDelay frames later to force a light-flag rebuild. */
	void RequestRefRefresh(RE::TESObjectREFR* refr);
	/** @brief Counts down a pending refresh and fires the deferred Enable; call once per frame before any early-out. */
	void UpdateRefRefresh();

	/** @brief Builds the cached LIGH form list on first use. */
	static void EnsureLighFormListBuilt();
	static std::vector<std::pair<std::string, RE::TESObjectLIGH*>> s_lighFormList;
	/** @brief Loads a LIGH form's data (color, flags, size, radius) into the current settings. */
	void ApplyLighFormData(const RE::TESObjectLIGH* ligh);

	/** @brief Builds the cached emittance-source form list on first use. */
	static void EnsureEmittanceFormListBuilt();
	static std::vector<std::pair<std::string, RE::TESForm*>> s_emittanceFormList;
	/** @brief Draws the shared External Emittance combo and applies the selection live (no-op without a backing ref). */
	void DrawExternalEmittanceCombo();
	/** @brief Sets the emittance source driving the selected bulb's color preview (nullptr clears it; does not modify the ref). */
	void ApplyExternalEmittance(RE::TESForm* source);
	/** @brief Clears the external-emittance editor state (combo shows None; source dropped on save). */
	void ClearExternalEmittance();
	/** @brief Tracks the selected bulb's color toward its emittance source's live color (no-op without a source). */
	void UpdateEmittanceColor();
	/** @brief Queues an identity-based re-selection of the current LP bulb so it survives a reloadlp. */
	void QueueReselectCurrentLP();

	/** @brief Resolves a "0xID~Plugin.esp" or "0xID" entry string to a FormID, or 0 on failure. */
	static RE::FormID ResolveFormEntry(const std::string& entry);
	/** @brief True if the flag set contains any shadow-casting flag. */
	static bool HasShadowFlags(uint32_t tesFlags);
	/** @brief Builds the combo display name for a light (FormID/index/pointer + name). */
	static std::string GetLightName(const LightInfo& lightInfo);
	/** @brief Pointer to the cached EditorID for a LIGH FormID (valid for process lifetime), or nullptr if not found. */
	static const std::string* LighEdidPtrForFormId(RE::FormID formId);
	/** @brief EditorID for a LIGH FormID from the cached form list, or "" if not found. */
	static std::string LighEdidForFormId(RE::FormID formId);
	/** @brief Parses an "LP_Light[config|EDID]" NiLight name into its parts (rejects path traversal). */
	static LPLightInfo ParseLPLightName(const std::string& name);
	/** @brief True if the light entry's whiteList/blackList resolve to include the given reference. */
	static bool MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr);
	/** @brief Saves the current editor state to the matching LP JSON entry (dryRun only checks for a match). */
	bool SaveToLightPlacer(bool includeColor = false, bool dryRun = false);
	/** @brief Forks the bulb into a whitelist-only entry for the selected ref and blacklists it in the original; false on no match. */
	bool SaveAsSeparateEntry(bool includeColor = false);
	/** @brief Deletes the bulb's matching light entry (and its top-level entry if it was the only light); false on no match. */
	bool DeleteFromLightPlacer();
	/** @brief Builds a fresh "data" object from editor state, carrying over unmanaged keys from existingData. */
	nlohmann::ordered_json BuildEditedData(const nlohmann::ordered_json& existingData, bool includeColor) const;
	/** @brief Re-orders every light's data/entry keys into the canonical layout. */
	static void NormalizeConfig(nlohmann::ordered_json& configArray);

	/** @brief Identifies an LP config entry to match: from the selected bulb or the Add-Light picked mesh. */
	struct MatchContext
	{
		std::string ownerModelPath;
		std::string ownerEditorId;
		RE::FormID baseFormId = 0;  // base object FormID of the owner ref
		std::string lightEDID;
		RE::TESObjectREFR* refr = nullptr;
	};

	/** @brief Builds a MatchContext from the currently selected LP bulb. */
	MatchContext MakeSelectedContext() const;
	/** @brief Builds a MatchContext from the Add-Light popup's picked mesh + the given light EDID. */
	MatchContext MakePickedContext(const std::string& lightEDID) const;
	/** @brief Adds or removes ownerEntry in a light entry's whiteList/blackList array. */
	static void MutateFilterList(nlohmann::ordered_json& lightEntry, const char* listKey, const std::string& ownerEntry, bool add);
	/** @brief Adds/removes the context's owner reference in the matching entry's filter list. */
	bool ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, bool isWhiteList, bool add);
	/** @brief Adds/removes an explicit entry string in the matching entry's filter list. */
	bool ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, const std::string& entryStr, bool isWhiteList, bool add);

	/** @brief Formats a reference's identity as an LP "0xID~Plugin.esp" entry string. */
	static std::string FormatOwnerFormEntry(RE::TESObjectREFR* refr);
	/** @brief Loads the selected bulb's LP config array into out. */
	bool LoadLPConfig(nlohmann::ordered_json& out) const;
	/** @brief True if a top-level config entry's models/formIDs identify the context's owner ref. */
	static bool EntryMatchesContext(const nlohmann::ordered_json& entry, const MatchContext& ctx);
	/** @brief Finds the light entry matching ctx (optionally applying LP filters), or nullptr. */
	nlohmann::ordered_json* FindMatchingLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, bool applyFilters = true) const;
	/** @brief Where a matched light entry lives: its top-level entry + index and "lights" array + index. */
	struct LightEntryLocation
	{
		nlohmann::ordered_json* topEntry = nullptr;
		size_t topIdx = 0;
		nlohmann::ordered_json* lightsArr = nullptr;
		size_t lightIdx = 0;
	};
	/** @brief Locates the light entry governing ctx (LP filters applied); false when none matches. */
	bool LocateLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, LightEntryLocation& out) const;
	/** @brief Adds/removes the selected reference in the current bulb's filter list. */
	bool ModifyLPFilterList(bool isWhiteList, bool add);
	/** @brief Reloads the selected bulb's filter/flag/emittance state from its LP JSON entry. */
	void RefreshLPJsonState();
	/** @brief Applies the LP flag set to the current runtime light flags. */
	void SyncLPFlagsToRuntime();
	/** @brief Mirrors the InverseSquare/Linear falloff bits of an LP flag set onto a runtime light's flags. */
	static void ApplyLPFalloffFlags(ISLCommon::RuntimeLightDataExt& data, const std::set<std::string>& lpFlagSet);

	/** @brief Snapshots and tracks the selected light each frame, applying editor edits and LP state. */
	void UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight, RE::BSLight* bsLight);

	// Add-Light-to-Mesh workflow state.
	LightPicker picker;
	bool addLightPopupOpen = false;
	LightPicker::PickedMesh pickedMesh;
	// The picked mesh's live reference, or nullptr if the handle is stale.
	RE::TESObjectREFR* PickedRefr() const { return pickedMesh.refrHandle.get().get(); }

	struct AttachedBulb
	{
		std::string lightEDID;
		std::string configPath;
		RE::FormID refrId = 0;  // owner reference (the picked mesh ref)
		uint32_t index = 0;     // running per-ref index, mirrors GatherLights ordering
	};
	std::vector<AttachedBulb> attachedBulbs;  // bulbs live-attached to pickedMesh, built on popup open
	int addSelectedBulb = -1;                 // index into attachedBulbs
	char addBulbSearch[256] = {};             // search text for the bulb combo
	bool editBulbComboPendingOpen = false;    // one-shot: open the Edit Bulb combo on mode entry

	struct FilterListEntry
	{
		std::string lightEDID;
		std::string configPath;
		std::string matchedEntry;  // the exact string found in whiteList/blackList
		bool isWhiteList = false;
	};
	std::vector<FilterListEntry> filterListEntries;  // WL/BL entries where pickedMesh's ref appears
	int addSelectedFilterEntry = -1;
	char addFilterSearch[256] = {};
	int addFilterEntryType = 0;  // 0 = Reference (FormID), 1 = Cell EditorID

	// Popup selections.
	std::vector<std::string> lpConfigPaths;  // relative paths under LightPlacer\, no extension
	int addSelectedConfig = -1;              // index into lpConfigPaths
	int addAttachMode = -1;                  // 0 = Model, 1 = FormID, 2 = EditorID
	RE::FormID addSelectedLighFormId = 0;    // chosen LIGH
	char addConfigSearch[256] = {};          // persisted search text for Target JSON combo
	char addLighSearch[256] = {};            // persisted search text for Light record combo
	int addPopupMode = -1;                   // 0 = Add Light, 1 = Edit Bulb, 2 = Whitelist, 3 = Blacklist, 4 = Remove from List
	enum AddPopupMode
	{
		ModeAddLight = 0,
		ModeEditBulb = 1,
		ModeWhitelist = 2,
		ModeBlacklist = 3,
		ModeRemoveFromList = 4
	};
	int addLightSubMode = -1;  // 0 = Add new point, 1 = Add to entry, 2 = Add new entry
	enum AddLightSubMode
	{
		SubModeNewPoint = 0,
		SubModeToEntry = 1,
		SubModeNewEntry = 2
	};
	bool addPopupPrefsLoaded = false;

	// Post-add attaching sequence. Each step is spaced by kAttachStepDelay so the game
	// has time to flush the disable/enable and respawn the reference with its new bulb.
	enum class AttachPhase
	{
		Idle,
		WaitingForReload,
		WaitingForEnable,
		WaitingForRespawn
	};
	static constexpr std::chrono::milliseconds kAttachStepDelay{ 500 };
	AttachPhase attachPhase = AttachPhase::Idle;
	std::chrono::steady_clock::time_point attachPhaseStart;
	RE::ObjectRefHandle attachPendingRefr;
	std::string attachConfigPath;

	// Auto-select the newly spawned LP light after the attaching sequence completes.
	bool pendingAutoSelect = false;
	int pendingAutoSelectTTL = 0;  // gather passes remaining before giving up
	RE::FormID pendingSelectRefrId = 0;
	std::string pendingSelectConfigPath;
	std::string pendingSelectLighEdid;

	/** @brief Draws the "Select Mesh" button that starts the light picker. */
	void DrawAddLightButton();
	/** @brief Draws the Add-Light modal (add/edit/whitelist/blacklist a bulb on the picked mesh). */
	void DrawAddLightPopup();
	/** @brief Draws the modal confirming deletion of the selected LP light entry. */
	void DrawDeleteConfirmation();
	/** @brief Opens a persistent-buffer searchable combo (Add-Light style); true while open, with the filter in filterOut. */
	static bool BeginSearchableCombo(const char* label, const char* preview, const char* searchId,
		char* searchBuf, size_t searchBufSize, std::string_view& filterOut, bool openNow);
	/** @brief True when an item should be shown for the given combo filter (empty matches everything). */
	static bool MatchesComboFilter(std::string_view filter, const std::string& text);
	/** @brief Shows a success- or error-styled notification for an operation result. */
	static void NotifyResult(bool ok, const char* okMsg, const char* failMsg);
	/** @brief Re-selects the current LP bulb by identity and reloads the LP configs so the selection survives. */
	void ReloadLPAndReselect();
	/** @brief Searchable "Attached bulb" combo; returns the index clicked this frame (or -1) and sets addSelectedBulb. */
	int DrawAttachedBulbCombo(const char* searchId, bool openNow);
	/** @brief Searchable "Light record" combo over the cached LIGH list; writes addSelectedLighFormId. */
	void DrawLightRecordCombo(const char* searchId);
	/** @brief Kicks off the timed reload/disable/enable/respawn sequence for the picked mesh. */
	void BeginAttachSequence(const std::string& configPath);
	/** @brief Scans Data\LightPlacer for config files, returning their extension-less relative paths. */
	std::vector<std::string> ScanLPConfigPaths() const;
	/** @brief Collects the LP bulbs live-attached to refr into attachedBulbs. */
	void GatherAttachedBulbs(RE::TESObjectREFR* refr);
	/** @brief Collects the whitelist/blacklist entries where refr appears into filterListEntries, scanning the given configs. */
	void ScanFilterListEntries(RE::TESObjectREFR* refr, const std::vector<std::string>& configPaths);
	/** @brief Validates the Add-Light selections; false with a reason when a bulb can't be added. */
	bool CanAddBulb(std::string& reasonOut) const;
	/** @brief The target string (model/formID/EditorID) for a new entry per the chosen attach mode. */
	std::string AddEntryTargetString() const;
	/** @brief Adds a new top-level entry with one bulb to the selected config. */
	bool AddBulbToConfig();
	/** @brief Adds an extra point to an existing bulb's entry. */
	bool AddPointToConfig(const AttachedBulb& bulb);
	/** @brief True if the bulb's matching entry already contains the given light EDID. */
	bool LightAlreadyInEntry(const AttachedBulb& bulb, const std::string& lighEdid) const;
	/** @brief Adds a light to an existing top-level entry matching the bulb. */
	bool AddLightToExistingEntry(const AttachedBulb& bulb, const std::string& lighEdid);
	/** @brief Persists the Add-Light popup preferences to disk. */
	void SavePopupPrefs() const;
	/** @brief Loads the Add-Light popup preferences from disk. */
	void LoadPopupPrefs();
};
