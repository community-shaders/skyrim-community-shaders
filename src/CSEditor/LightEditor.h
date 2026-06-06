#pragma once
#include "../Features/InverseSquareLighting/Common.h"
#include "LightPicker.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <set>

namespace RE
{
	class BSLight;
}

struct LightEditor
{
	bool disableInvSqLights = false;
	bool disableRegularLights = false;
	bool shadowsOnly = false;

	void DrawSettings();
	void GatherLights();
	void ResetOverrides();

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

	const char* FilterOptionLabels[3] = {
		"Ref Lights",
		"Attached Lights",
		"Other Lights"
	};

	enum class SortOption
	{
		None,
		Distance,
		FormID,
		EditorID,
		Count
	};

	const char* SortOptionLabels[4] = {
		"None",
		"Distance",
		"FormID",
		"EditorID"
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

	// External-emittance color preview (CS-driven, selected bulb only). The game only drives
	// emittance color for references registered in the cell's emittance maps (e.g. LP bulbs); a
	// reference we patch an ExtraEmittanceSource onto isn't, so its color never follows the source.
	// While such a bulb is selected we drive it ourselves: each frame we lerp the displayed color
	// toward the source region's live emittanceColor, replacing the base color. Reverts on deselect.
	RE::TESForm* activeEmittanceSource = nullptr;
	bool emittanceColorActive = false;  // true once seeded; gates ApplyOverrides to the lerped color
	RE::NiColor emittanceColorLerped{};
	std::chrono::steady_clock::time_point emittanceLastUpdate{};
	static constexpr float kEmittanceLerpTau = 0.5f;  // seconds; exponential-smoothing time constant

	// Deferred 3D rebuild after a light-flag edit. The engine's despawn (Disable -> unload 3D)
	// and respawn (Enable -> load 3D) are async tasks; issued back-to-back in the same frame they
	// can complete out of order (unload after load), leaving the reference's 3D unloaded and the
	// mesh stuck disabled. We Disable now and Enable a few frames later, coalescing repeated edits
	// on the same reference into one pending rebuild (mirrors the spaced disable/enable the attach
	// sequence already uses). A pending refresh on a different reference is flushed first so we
	// never strand a previously-disabled reference.
	RE::ObjectRefHandle pendingRefreshRefr;
	int32_t pendingRefreshFrames = 0;
	static constexpr int32_t kRefreshEnableDelay = 3;

	float shadowDepthBias = 0.0f;
	float originalShadowDepthBias = 0.0f;
	float cachedFadeBeforeToggle = 0.0f;

	void SortLights();
	void RestoreOriginal();
	void ApplyShadowDepthBias();
	// Disables refr now and schedules its Enable kRefreshEnableDelay frames later (see members
	// above). Use instead of a same-frame Disable()/Enable() pair to force a light-flag rebuild.
	void RequestRefRefresh(RE::TESObjectREFR* refr);
	// Counts down a pending refresh and fires the deferred Enable. Call once per frame, before any
	// early-out, so the Enable still runs while resampling is paused or menu focus is lost.
	void UpdateRefRefresh();

	static void EnsureLighFormListBuilt();
	static std::vector<std::pair<std::string, RE::TESObjectLIGH*>> s_lighFormList;
	void ApplyLighFormData(const RE::TESObjectLIGH* ligh);

	static void EnsureEmittanceFormListBuilt();
	static std::vector<std::pair<std::string, RE::TESForm*>> s_emittanceFormList;
	// Draws the shared External Emittance combo for the active reference (any bulb type) and applies
	// the selection live. Self-gates: does nothing when the selection has no backing reference.
	void DrawExternalEmittanceCombo();
	// Sets/swaps/clears the reference's runtime ExtraEmittanceSource (pass nullptr to clear) and
	// refreshes the light so the change is visible immediately.
	void ApplyExternalEmittance(RE::TESObjectREFR* refr, RE::TESForm* source);
	// Advances the per-frame lerp of the selected bulb's color toward its emittance source's live
	// color. No-op (and clears the active flag) when there is no emittance source.
	void UpdateEmittanceColor();

	static RE::FormID ResolveFormEntry(const std::string& entry);
	static bool HasShadowFlags(uint32_t tesFlags);
	static std::string GetLightName(const LightInfo& lightInfo);
	// EditorID for a LIGH FormID from the cached form list, or "" if not found.
	static std::string LighEdidForFormId(RE::FormID formId);
	static LPLightInfo ParseLPLightName(const std::string& name);
	static bool MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr);
	bool SaveToLightPlacer(bool includeColor = false, bool dryRun = false);
	// Forks the bulb's matching light entry into a new whitelist-only entry for the selected
	// reference (capturing the current editor edits) and blacklists that reference in the
	// original entry, so the edits apply solely to this reference. Returns false on no match.
	bool SaveAsSeparateEntry(bool includeColor = false);
	// Builds a fresh "data" object from the current editor state, carrying over any unmanaged
	// keys from existingData. Shared by SaveToLightPlacer and SaveAsSeparateEntry.
	nlohmann::ordered_json BuildEditedData(const nlohmann::ordered_json& existingData, bool includeColor) const;
	// Re-orders the data/light-entry keys of every light in the config into the canonical layout.
	static void NormalizeConfig(nlohmann::ordered_json& configArray);

	// Context used to match an LP config entry. Defaults (via MakeSelectedContext) reproduce
	// the historical member-driven behavior; the Select-Mesh popup builds one from the picked
	// mesh + a chosen attached bulb instead.
	struct MatchContext
	{
		std::string ownerModelPath;
		std::string ownerEditorId;
		RE::FormID baseFormId = 0;  // base object FormID of the owner ref
		std::string lightEDID;
		RE::TESObjectREFR* refr = nullptr;
	};

	MatchContext MakeSelectedContext() const;
	static void MutateFilterList(nlohmann::ordered_json& lightEntry, const char* listKey, const std::string& ownerEntry, bool add);
	bool ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, bool isWhiteList, bool add);
	bool ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, const std::string& entryStr, bool isWhiteList, bool add);

	static std::string FormatOwnerFormEntry(RE::TESObjectREFR* refr);
	bool LoadLPConfig(nlohmann::ordered_json& out) const;
	// True if a top-level config entry's models/formIDs identify the context's owner ref.
	static bool EntryMatchesContext(const nlohmann::ordered_json& entry, const MatchContext& ctx);
	nlohmann::ordered_json* FindMatchingLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, bool applyFilters = true) const;
	bool ModifyLPFilterList(bool isWhiteList, bool add);
	void RefreshLPJsonState();
	void SyncLPFlagsToRuntime();

	void UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight, RE::BSLight* bsLight);

	// Add-Light-to-Mesh workflow state.
	LightPicker picker;
	bool addLightPopupOpen = false;
	LightPicker::PickedMesh pickedMesh;

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

	void DrawAddLightButton();
	void DrawAddLightPopup();
	// Searchable "Attached bulb" combo over attachedBulbs. Returns the index clicked this
	// frame (or -1); sets addSelectedBulb on click. openOnAppear opens the dropdown on first show.
	int DrawAttachedBulbCombo(const char* searchId, bool openOnAppear);
	// Searchable "Light record" combo over the cached LIGH list; writes addSelectedLighFormId.
	void DrawLightRecordCombo(const char* searchId);
	// Kicks off the timed reload/disable/enable/respawn sequence for the picked mesh.
	void BeginAttachSequence(const std::string& configPath);
	std::vector<std::string> ScanLPConfigPaths() const;
	void GatherAttachedBulbs(RE::TESObjectREFR* refr);
	void ScanFilterListEntries(RE::TESObjectREFR* refr);
	bool CanAddBulb(std::string& reasonOut) const;
	std::string AddEntryTargetString() const;
	bool AddBulbToConfig();
	bool AddPointToConfig(const AttachedBulb& bulb);
	bool LightAlreadyInEntry(const AttachedBulb& bulb, const std::string& lighEdid) const;
	bool AddLightToExistingEntry(const AttachedBulb& bulb, const std::string& lighEdid);
	void SavePopupPrefs() const;
	void LoadPopupPrefs();
};
