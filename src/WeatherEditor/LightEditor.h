#pragma once
#include "../Features/InverseSquareLighting/Common.h"

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
		uint32_t id;
		void* ptr;
		uint32_t index;
		std::string name;
		bool isRef;
		bool isAttached;
		bool isOther;
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

	bool showAttachedLights = false;
	bool showEffectLights = false;
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
	RE::TESObjectREFR* activeRefr = nullptr;
	RE::TESObjectLIGH* activeLigh = nullptr;
	bool activeIsRef = false;

	void SortLights();
	void RestoreOriginal();

	static std::string GetLightName(LightInfo& lightInfo);
	static LPLightInfo ParseLPLightName(const std::string& name);
	static std::string UpdateLPFlags(const std::string& existingFlags, bool inverseSquare, bool linear);
	static bool MatchesLPFilters(const json& lightEntry, RE::TESObjectREFR* refr);
	static std::array<float, 3> GetJsonVec3(const json& data, const char* key);
	bool SaveToLightPlacer();

	void UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight);
};
