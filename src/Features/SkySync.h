#pragma once
#include "RE/M/Moon.h"

#include "Utils/Moon.h"

struct SkySync : Feature
{
private:
	static constexpr std::string_view MOD_ID = "153543";

public:
	virtual inline std::string GetName() override { return "Sky Sync"; }
	virtual std::string GetDisplayName() override { return T("feature.sky_sync.name", "Sky Sync"); }
	virtual inline std::string GetShortName() override { return "SkySync"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kSky; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.sky_sync.description", "Synchronizes volumetric lighting and shadows with the actual sun and moon positions in the sky."),
			{ T("feature.sky_sync.key_feature_1", "Fixes the mismatch between the positions of the sun and moons and the lighting direction"),
				T("feature.sky_sync.key_feature_2", "Includes a configurable alternative sun path for more realistic and dramatic lighting"),
				T("feature.sky_sync.key_feature_3", "Smoothly switches the light source between the sun and moons based on visibility"),
				T("feature.sky_sync.key_feature_4", "Moon light source can be switched between Masser, Secunda, or the brightest"),
				T("feature.sky_sync.key_feature_5", "Automatic calculation of moon lighting intensity based on moon phase"),
				T("feature.sky_sync.key_feature_6", "Fixes the sun appearing higher on the horizon when the player gains altitude") } };
	};

	struct Settings
	{
		bool Enabled = true;
		bool UseAlternateSunPath = false;
		int32_t MoonLightSource = 0;
		int32_t SunPath = 0;
		float CustomAngle = -35.0f;
		float MinShadowElevation = 18.0f;
		float ShadowTransitionDuration = 100.0f;
		bool DimSunlightUnderHorizon = true;
		float NewMoonIntensity = 0.05f;
		float CrescentMoonIntensity = 0.25f;
		float FullMoonIntensity = 1.0f;
	};

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; }

	void OnSkyUpdateColors(RE::Sky* sky);

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	struct Sky_Update
	{
		static void thunk(RE::Sky* sky);
		static inline REL::Relocation<decltype(thunk)> func;
	};


private:
	enum class CellFlagExt : uint16_t
	{
		kSunlightShadows = 1 << 15,
	};

	enum class MoonLightSource : uint8_t
	{
		Brightest,
		Masser,
		Secunda,
		Count
	};

	enum class Caster : uint8_t
	{
		Sun,
		Masser,
		Secunda,
		None
	};

	enum class SunPath : uint8_t
	{
		Southern,
		Northern,
		Vanilla,
		Custom,
		Count
	};

	const char* MoonLightSourceNames[static_cast<uint8_t>(MoonLightSource::Count)] = { "Brightest", "Masser", "Secunda" };
	const char* SunPathNames[static_cast<uint8_t>(SunPath::Count)] = { "Southern Sky", "Northern Sky", "Vanilla", "Custom" };

	struct ShadowFader
	{
		RE::NiPoint3 currentDir = { 0.0f, 0.0f, 1.0f };
		RE::NiPoint3 startDir = { 0.0f, 0.0f, 1.0f };
		Caster target = Caster::Sun;
		Caster previousTarget = Caster::Sun;
		float fadeTimer = 0.0f;
		bool transitioning = false;

		void Update(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[], float fadeDuration, float fadeAdvance);
		static void SetLighting(const RE::Sky* sky, RE::NiPoint3 dir);
		static void ClampDirection(RE::NiPoint3& dir);
		void Reset();
	};

	static constexpr float SunHorizonDistance = 280.0f;
	static constexpr float SunPeakDistance = 400.0f;
	static constexpr float SouthernSunAngle = 90.0f - 35.0f;
	static constexpr float NorthernSunAngle = 90.0f + 35.0f;
	static constexpr float VanillaSunAngle = 90.0f + 5.0f;
	static constexpr float SecondsPerGameHour = 3600.0f;

	inline static RE::NiPoint3* gSunPosition = nullptr;

	bool moonAndStarsLoaded = false;
	RE::TESObjectCELL* currentCell = nullptr;
	float sunAngle = 90.0f;
	float currentSkyRotation = D3D11_FLOAT32_MAX;
	float lastGameHour = -1.0f;

	float4 colors[3] = {};
	float currentDim = 1.0f;
	ShadowFader shadowFader;

	void DisableOnConflict(std::string_view conflictName);

	void Update(const RE::Sky* sky);

	void SetSunAngle();

	void SetSkyRotation(const RE::Sky* sky, RE::TESObjectCELL* cell);

	void ProcessSun(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[]);

	void ProcessMoon(const RE::Sky* sky, Caster type, RE::NiPoint3 dirs[], float intensities[]);

	static bool IsNight(const RE::Sky* sky);
	static bool IsDaytime(const RE::Sky* sky);

	static void CalculateSunDirectionAndDistance(const RE::Sun* sun, RE::NiPoint3& outDir, float& outDistance);

	static void CalculateAlternateSunDirectionAndDistance(RE::NiPoint3& outDir, float& outDist, float time, float sunrise, float sunset, float sunAngle);

	static void SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, float distance);
};
