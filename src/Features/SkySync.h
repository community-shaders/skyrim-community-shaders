#pragma once
#include "RE/M/Moon.h"

struct SkySync : Feature
{
private:
	static constexpr std::string_view MOD_ID = "153543";

public:
	virtual inline std::string GetName() override { return "Sky Sync"; }
	virtual inline std::string GetShortName() override { return "SkySync"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kSky; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Synchronizes volumetric lighting and shadows with the actual sun and moon positions in the sky.",
			{ "Fixes the mismatch between the positions of the sun and moons and the lighting direction",
				"Includes a configurable alternative sun path for more realistic and dramatic lighting",
				"Smoothly switches the light source between the sun and moons based on visibility",
				"Moon light source can be switched between Masser, Secunda, or the brightest",
				"Automatic calculation of moon lighting intensity based on moon phase",
				"Fixes the sun appearing higher on the horizon when the player gains altitude" }
		};
	}

	struct Settings
	{
		bool Enabled = true;
		bool UseAlternateSunPath = true;
		int32_t MoonLightSource = 0;
		int32_t SunPath = 0;
		float CustomAngle = -35.0f;
		float SunriseBeginOffset = 0.0f;
		float SunriseEndOffset = 0.0f;
		float SunsetBeginOffset = 0.0f;
		float SunsetEndOffset = 0.0f;
		float MinShadowElevation = 0.25f;
	};

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; }
	virtual bool SupportsVR() override { return true; }

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	struct Sky_Update
	{
		static void thunk(RE::Sky* sky);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Sky_OnNewClimate
	{
		static void thunk(RE::Sky* sky);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Moon_Update
	{
		static void thunk(RE::Moon* moon, RE::Sky* sky);
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

	struct ClimateTimings
	{
		float sunriseFadeOutMoonStart;
		float sunriseBegin;
		float sunriseFadeOutMoonEnd;
		float sunrise;
		float sunriseEnd;
		float sunsetBegin;
		float sunset;
		float sunsetFadeInMoonStart;
		float sunsetEnd;
		float sunsetFadeInMoonEnd;

		void Update(const RE::TESClimate* climate);
	};

	struct ShadowFader
	{
		enum class Phase : uint8_t
		{
			None,
			FadeOut,
			FadeIn
		};

		static constexpr float FadeTime = 100.0f;  // 5 seconds at timescale 20

		Phase fadePhase = Phase::None;
		Caster current = Caster::None;
		Caster target = Caster::None;
		float fadeTimer = 0.0f;
		float previousHoursPassed = 0.0f;

		void Update(const RE::Sun* sun, RE::NiPoint3 dirs[], float intensities[], bool isDayTime);
		static void SetLighting(const RE::Sun* sun, RE::NiPoint3 dir, float intensity);
		static void ClampDirection(RE::NiPoint3& dir);
		void Reset();
	};

	static constexpr float RenderDistance = 325000.0f;
	static constexpr float SunHorizonDistance = 280.0f;
	static constexpr float SunPeakDistance = 400.0f;
	static constexpr float SunScaleFactor = 48.0f / 2048.0f;

	static constexpr float SouthernSunAngle = 90.0f - 35.0f;
	static constexpr float NorthernSunAngle = 90.0f + 35.0f;
	static constexpr float VanillaSunAngle = 90.0f + 5.0f;

	static constexpr float SecundaIntensityFactor = 0.67f;
	static constexpr float NewMoonIntensityFactor = 0.05f;
	static constexpr float CrescentMoonIntensityFactor = 0.25f;
	static constexpr float FullMoonIntensityFactor = 1.0f;

	inline static RE::NiPoint3* gSunPosition = nullptr;
	inline static float* gSunGlareSize = nullptr;
	inline static uint32_t* gMasserSize = nullptr;
	inline static uint32_t* gSecundaSize = nullptr;

	bool moonAndStarsLoaded = false;
	RE::TESObjectCELL* currentCell = nullptr;
	float sunAngle = 90.0f;
	float currentSkyRotation = D3D11_FLOAT32_MAX;
	float masserPhaseIntensityFactor = 0.0f;
	float secundaPhaseIntensityFactor = 0.0f;

	ClimateTimings timings = {};

	RE::NiPoint3 rawDirections[3];
	RE::NiPoint3 directions[3];
	float intensities[3] = {};
	ShadowFader shadowFader;

	void DisableOnConflict(std::string_view conflictName);

	void Update(const RE::Sky* sky);

	void SetSunAngle();

	void SetSkyRotation(const RE::Sky* sky, RE::TESObjectCELL* cell);

	void ProcessSun(const RE::Sun* sun, float time, float altitude, bool isDayTime);

	void ProcessMoon(const RE::Moon* moon, float time, Caster type, float altitude, bool isDayTime);

	static void CalculateSunDirectionAndDistance(const RE::Sun* sun, RE::NiPoint3& outDir, float& outDistance);

	static void CalculateAlternateSunDirectionAndDistance(RE::NiPoint3& outDir, float& outDist, float time, float sunrise, float sunset, float sunAngle);

	static RE::NiPoint3 GetApparentDirection(const RE::NiPoint3& dir, float altitude);

	static void SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, float distance);

	static void SetMoonDirection(const RE::Moon* moon, const RE::NiPoint3& dir);

	static float CalculateVisibility(const RE::NiPoint3& dir, float dist, float radius);

	static void SetSunBaseVisibility(const RE::Sun* sun, float visibility);

	static float SmoothStep(float start, float end, float x);
};
