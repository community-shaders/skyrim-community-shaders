#pragma once
#include "RE/M/Moon.h"

#include "Utils/Moon.h"

/** @brief Synchronizes volumetric lighting and shadow direction with actual sun and moon positions. */
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

	/** @brief Returns a description and list of key features for the UI summary. */
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
		float MinShadowElevation = 10.0f;
		float ShadowTransitionDuration = 100.0f;
		bool DimSunlightUnderHorizon = true;
		bool DimVolumetricLighting = true;
		float HorizonFadeHours = 0.7f;
		float NewMoonIntensity = 0.05f;
		float CrescentMoonIntensity = 0.25f;
		float FullMoonIntensity = 1.0f;
	};

	Settings settings;

	/** @brief Draws the ImGui settings panel for Sky Sync configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; }

	/**
	 * @brief Dims sunlight color when the sun is below the horizon during sky color updates.
	 * @param sky The sky object whose directional light color may be modified.
	 */
	void OnSkyUpdateColors(RE::Sky* sky);

	/** @brief Installs rendering hooks and detects conflicting mods after plugin load. */
	virtual void PostPostLoad() override;
	/** @brief Checks for conflicting ESP files after game data is loaded. */
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
		bool sunriseReleased = false;
		float frozenHeading = 0.0f;
		bool sunsetHeadingLocked = false;
		float vlIntensityFactor = 1.0f;

		void Update(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[], float fadeDuration, float fadeAdvance);
		void LockSunElevation(RE::NiPoint3 dirs[]);
		static void SetLighting(const RE::Sky* sky, RE::NiPoint3 dir);
		static void SetDirection(RE::NiPoint3& dir, float headingRadians, float elevRadians);
		static void SetElevation(RE::NiPoint3& dir, float elevRadians);
		static void ClampDirection(RE::NiPoint3& dir);
		static float ComputeVLFactor(const RE::NiPoint3& current, const RE::NiPoint3& target);
		void Reset();
	};

	static constexpr float SunHorizonDistance = 280.0f;
	static constexpr float SunPeakDistance = 400.0f;
	static constexpr float SouthernSunAngle = 90.0f - 35.0f;
	static constexpr float NorthernSunAngle = 90.0f + 35.0f;
	static constexpr float VanillaSunAngle = 90.0f + 5.0f;
	static constexpr float SecondsPerGameHour = 3600.0f;
	static constexpr float SunsetHeadingLockThreshold = 0.5f;
	static constexpr float VLFadeStartAngle = 2.0f;
	static constexpr float VLFadeEndAngle = 10.0f;
	static constexpr float MaxHorizonFadeHours = 1.5f;

	inline static RE::NiPoint3* gSunPosition = nullptr;
	inline static RE::BSVolumetricLightingRenderData* gVolumetricLighting = nullptr;

	bool moonAndStarsLoaded = false;
	RE::TESObjectCELL* currentCell = nullptr;
	float sunAngle = 90.0f;
	float currentSkyRotation = D3D11_FLOAT32_MAX;
	float lastGameHour = -1.0f;

	float4 colors[3] = {};
	float currentDim = 1.0f;
	bool sunSetting = false;
	bool sunRising = false;
	bool sunBelowHorizon = false;
	ShadowFader shadowFader;

	void DisableOnConflict(std::string_view conflictName);

	void Update(const RE::Sky* sky);

	void SetSunAngle();

	void SetSkyRotation(const RE::Sky* sky, RE::TESObjectCELL* cell);

	void ProcessSun(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[]);

	void ProcessMoon(const RE::Sky* sky, Caster type, RE::NiPoint3 dirs[], float intensities[]);

	static void CalculateSunDirectionAndDistance(const RE::Sun* sun, RE::NiPoint3& outDir, float& outDistance);

	static void CalculateAlternateSunDirectionAndDistance(RE::NiPoint3& outDir, float& outDist, float time, float sunrise, float sunset, float sunAngle);

	static void SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, float distance);
};
