#pragma once
#include "../Features/InverseSquareLighting/Common.h"
#include "LightPicker.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <set>

namespace RE { class BSLight; }

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
		RE::FormID ownerFormId = 0;
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

	float shadowDepthBias = 0.0f;
	float originalShadowDepthBias = 0.0f;
	float cachedFadeBeforeToggle = 0.0f;

	void SortLights();
	void RestoreOriginal();
	void ApplyShadowDepthBias();

	static void EnsureLighFormListBuilt();
	static std::vector<std::pair<std::string, RE::TESObjectLIGH*>> s_lighFormList;
	void ApplyLighFormData(const RE::TESObjectLIGH* ligh);

	static void EnsureEmittanceFormListBuilt();
	static std::vector<std::pair<std::string, RE::TESForm*>> s_emittanceFormList;

	static RE::FormID ResolveFormEntry(const std::string& entry);
	static bool HasShadowFlags(uint32_t tesFlags);
	static std::string GetLightName(LightInfo& lightInfo);
	static LPLightInfo ParseLPLightName(const std::string& name);
	static bool MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr);
	bool SaveToLightPlacer(bool includeColor = false, bool dryRun = false);

	// Context used to match an LP config entry. Defaults (via MakeSelectedContext) reproduce
	// the historical member-driven behavior; the Select-Mesh popup builds one from the picked
	// mesh + a chosen attached bulb instead.
	struct MatchContext
	{
		std::string ownerModelPath;
		std::string ownerEditorId;
		RE::FormID  baseFormId = 0;     // base object FormID of the owner ref
		std::string lightEDID;
		RE::TESObjectREFR* refr = nullptr;
	};

	MatchContext MakeSelectedContext() const;
	static void MutateFilterList(nlohmann::ordered_json& lightEntry, const char* listKey, const std::string& ownerEntry, bool add);
	bool ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, bool isWhiteList, bool add);

	static std::string FormatOwnerFormEntry(RE::TESObjectREFR* refr);
	bool LoadLPConfig(nlohmann::ordered_json& out) const;
	nlohmann::ordered_json* FindMatchingLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, bool applyFilters = true);
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
		RE::FormID  refrId = 0;   // owner reference (the picked mesh ref)
		uint32_t    index  = 0;   // running per-ref index, mirrors GatherLights ordering
	};
	std::vector<AttachedBulb> attachedBulbs;   // bulbs live-attached to pickedMesh, built on popup open
	int  addSelectedBulb = -1;                 // index into attachedBulbs
	char addBulbSearch[256] = {};              // search text for the bulb combo

	// Popup selections.
	std::vector<std::string> lpConfigPaths;   // relative paths under LightPlacer\, no extension
	int  addSelectedConfig = -1;              // index into lpConfigPaths
	int  addAttachMode = -1;                  // 0 = Model, 1 = FormID, 2 = EditorID
	RE::FormID addSelectedLighFormId = 0;     // chosen LIGH
	char addConfigSearch[256] = {};           // persisted search text for Target JSON combo
	char addLighSearch[256] = {};             // persisted search text for Light record combo
	int  addPopupMode = -1;                   // 0 = Add Light, 1 = Edit Bulb, 2 = Whitelist, 3 = Blacklist
	enum AddPopupMode { ModeAddLight = 0, ModeEditBulb = 1, ModeWhitelist = 2, ModeBlacklist = 3 };
	bool addPopupPrefsLoaded = false;

	// Post-add attaching sequence. Each step is spaced by kAttachStepDelay so the game
	// has time to flush the disable/enable and respawn the reference with its new bulb.
	enum class AttachPhase { Idle, WaitingForReload, WaitingForEnable, WaitingForRespawn };
	static constexpr std::chrono::milliseconds kAttachStepDelay{ 500 };
	AttachPhase attachPhase = AttachPhase::Idle;
	std::chrono::steady_clock::time_point attachPhaseStart;
	RE::ObjectRefHandle attachPendingRefr;
	std::string attachConfigPath;

	// Auto-select the newly spawned LP light after the attaching sequence completes.
	bool pendingAutoSelect = false;
	int pendingAutoSelectTTL = 0;     // gather passes remaining before giving up
	RE::FormID pendingSelectRefrId = 0;
	std::string pendingSelectConfigPath;
	std::string pendingSelectLighEdid;

	void DrawAddLightButton();
	void DrawAddLightPopup();
	std::vector<std::string> ScanLPConfigPaths() const;
	void GatherAttachedBulbs(RE::TESObjectREFR* refr);
	bool CanAddBulb(std::string& reasonOut) const;
	std::string AddEntryTargetString() const;
	bool AddBulbToConfig();
	void SavePopupPrefs() const;
	void LoadPopupPrefs();
};
