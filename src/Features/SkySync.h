#pragma once
#include <array>
#include <optional>

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
		float MinShadowElevation = 7.0f;
		float ShadowTransitionDuration = 300.0f;
		bool DimSunlightUnderHorizon = true;
		bool DimVolumetricLighting = true;
		float HorizonFadeHours = 0.7f;
		float HorizonFadeElevation = 7.0f;
		float HorizonElevation = -1.0f;
		float NewMoonIntensity = 0.05f;
		float CrescentMoonIntensity = 0.25f;
		float FullMoonIntensity = 1.0f;
		// Editor IDs; the defaults are realms whose weathers keep the same light around the clock
		std::vector<std::string> DisabledWorldspaces = { "Blackreach", "AlftandWorld", "Sovngarde", "DLC01SoulCairn", "DLC01Boneyard", "DLC2ApocryphaWorld" };
	};

	Settings settings;

	/** @brief Draws the ImGui settings panel for Sky Sync configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; }

	/**
	 * @brief Dims sunlight color when the sun is below the horizon during sky color updates, leaving lightning undimmed.
	 * @param sky The sky object whose directional light color may be modified.
	 */
	void OnSkyUpdateColors(RE::Sky* sky);

	/** @brief While the sun is below the horizon, rebuilds the moon glare, plus the sunlight colour and VL when they are dimmed, from the weathers' night values only; runs before colour grading. */
	void ApplyMoonlightColors(RE::Sky* sky);

	/** @brief How far this frame's moon glare was moved to the night colour, 0 to 1, so moon grading can follow it. */
	float GetMoonNightWeight() const { return moonNightWeight; }

	/** @brief 1 when this frame's sunlight was rebuilt from night colours, else 0, so light grading can follow it. */
	float GetLightNightWeight() const { return lightNightWeight; }

	/** @brief Installs rendering hooks and detects conflicting mods after plugin load. */
	virtual void PostPostLoad() override;
	/** @brief Checks for conflicting ESP files after game data is loaded. */
	virtual void DataLoaded() override;
	/** @brief Requests immediate celestial synchronization after loading an existing save. */
	virtual void GameLoaded() override;

	struct Sky_Update
	{
		static void thunk(RE::Sky* sky, float deltaTime);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	enum class Caster : uint8_t
	{
		Sun,
		Masser,
		Secunda,
		Zenith  // fallback when no moon is up: lights as a pseudo skylight, without direct light or VL
	};

	/**
	 * @brief Gets the world-space direction towards a sun or moon, preferring the one Sky Sync computed this frame.
	 * @param sky The sky whose root rotation maps Sky Sync's directions into world space.
	 * @param caster The celestial body to query; must not be Caster::Zenith.
	 * @return The unit direction, or straight up (0,0,1) if none is available.
	 */
	RE::NiPoint3 GetCelestialDirection(const RE::Sky* sky, Caster caster) const;

	/**
	 * @brief Moon selected by the "Moon light source" setting, when it is currently visible.
	 * @return Caster::Masser or Caster::Secunda, or Caster::Zenith when the chosen moon is hidden or faded out.
	 */
	Caster GetMoonLightCaster(const RE::Sky* sky) const;

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

	enum class SunPath : uint8_t
	{
		Southern,
		Northern,
		Vanilla,
		Custom,
		Count
	};

	struct ShadowFader
	{
		RE::NiPoint3 currentDir = { 0.0f, 0.0f, 1.0f };
		RE::NiPoint3 startDir = { 0.0f, 0.0f, 1.0f };
		RE::NiPoint3 dimDir = { 0.0f, 0.0f, 1.0f };  // direction whose elevation drives the horizon dim
		Caster target = Caster::Sun;
		Caster source = Caster::Sun;  // target before the current transition
		float fadeTimer = 0.0f;
		float immediateTransitionRemaining = 0.0f;
		bool transitioning = false;
		float intensityFactor = 1.0f;  // scales directional light and VL while the caster changes
		float startIntensityFactor = 1.0f;
		float casterFade = 1.0f;  // additionally scales directional light and VL, fading them out towards the zenith fallback
		float startCasterFade = 1.0f;

		/** @brief Fraction of the current transition completed, 0 to 1. */
		float Progress(float fadeDuration) const { return fadeDuration > 0.0f ? fadeTimer / fadeDuration : 1.0f; }

		void Update(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[], std::optional<std::array<RE::NiColor, 3>> colors, float fadeDuration, float fadeAdvance, bool a_immediateTransition);
		static void SetLighting(const RE::Sky* sky, RE::NiPoint3 dir, float intensity, std::optional<RE::NiColor> color);
		static void SetDirection(RE::NiPoint3& dir, float headingRadians, float elevRadians);
		static void SetElevation(RE::NiPoint3& dir, float elevRadians);
		static void ClampDirection(RE::NiPoint3& dir);
		static float ComputeAlignmentFactor(const RE::NiPoint3& current, const RE::NiPoint3& target);
		void Reset();
	};

	static constexpr float SunHorizonDistance = 280.0f;
	static constexpr float SunPeakDistance = 400.0f;
	static constexpr float SouthernSunAngle = 90.0f - 35.0f;
	static constexpr float NorthernSunAngle = 90.0f + 35.0f;
	static constexpr float VanillaSunAngle = 90.0f + 5.0f;
	static constexpr float SecondsPerGameHour = 3600.0f;
	static constexpr float AlignmentFadeStartAngle = 2.0f;
	static constexpr float AlignmentFadeEndAngle = 15.0f;
	static constexpr float MaxHorizonFadeHours = 1.5f;
	static constexpr float DefaultSunAlphaTransTime = 2.0f;
	static constexpr float AlternateSunHorizonOffsetHours = 0.25f;
	static constexpr float MaxHorizonFadeElevation = 45.0f;
	static constexpr float MinHorizonElevation = -10.0f;
	static constexpr float HoursPerDay = 24.0f;
	static constexpr float VanillaSunOffsetY = 25.0f;  // Sun::Update's fixed sideways offset of the sun's arc
	static constexpr int SunCrossingSearchIterations = 16;
	static constexpr float SunDiscFadeStartElevation = -2.0f;  // relative to the effective horizon
	static constexpr float SunDiscFadeElevation = 2.0f;
	static constexpr float SunGlareScale = 1.0f / 255.0f;  // Sun::Update's weather sun glare byte to alpha
	static constexpr RE::NiPoint3 ZenithDirection{ 0.0f, 0.0f, 1.0f };
	static constexpr size_t BodyCount = static_cast<size_t>(Caster::Zenith);  // the sun and both moons

	inline static RE::NiPoint3* gSunPosition = nullptr;
	inline static RE::BSVolumetricLightingRenderData* gVolumetricLighting = nullptr;
	inline static RE::Setting* gSunAlphaTransTime = nullptr;
	inline static RE::Setting* gWeatherFlashDirectional = nullptr;

	RE::TESObjectCELL* currentCell = nullptr;
	bool currentCellInterior = false;
	RE::TESWorldSpace* currentCellWorldspace = nullptr;
	bool worldspaceDisabled = false;  // currentCellWorldspace is in DisabledWorldspaces
	bool active = false;              // the last Update steered the light, so the colour hooks may apply its state
	float sunAngle = 90.0f;
	float currentSkyRotation = D3D11_FLOAT32_MAX;
	float lastGameHour = -1.0f;
	bool immediateTransitionReady = false;

	float4 colors[BodyCount] = {};
	RE::NiPoint3 rawDirections[BodyCount] = {};  // sky-local, before shadow elevation locking; zero when not computed this frame
	float currentDim = 1.0f;
	std::optional<std::array<RE::NiColor, 3>> workingLightColors = {};
	float moonlightFade = 0.0f;
	float twilightCap = 0.0f;
	float moonNightWeight = 0.0f;
	float lightNightWeight = 0.0f;
	float flashWeight = 0.0f;  // lightning's share of this frame's directional light
	bool sunBelowHorizon = false;
	ShadowFader shadowFader;

	void DisableOnConflict(std::string_view conflictName);

	void PreparePendingTransitions();

	bool Update(const RE::Sky* sky);

	/** @brief Signed angle in radians of a sky-local direction above the effective horizon. */
	float GetHeightAboveHorizon(const RE::NiPoint3& dir) const;

	/** @brief Fades from 0 at the effective horizon to 1 at the horizon fade elevation above it. */
	float GetHorizonDim(float heightAboveHorizon) const;

	/** @brief Fraction of the moonlight fade duration that the given hours cover; 1 when the duration is zero. */
	float GetMoonlightFadeProgress(float hours) const { return settings.HorizonFadeHours > 0.0f ? hours / settings.HorizonFadeHours : 1.0f; }

	/** @brief Ramps moonlightFade in while the sun is below the horizon and out otherwise, never above twilightCap. */
	void UpdateMoonlightFade(float advanceHours, bool immediate);

	/** @brief Upper bound on moonlight: zero at the sun's handover, rising over the moonlight fade duration after sunset and falling with the sun's depth before sunrise. */
	float GetTwilightCap(const RE::TESClimate::Timing& timing, float hour, float sunHeight) const;

	/** @brief Hour at which the sun crosses an elevation in radians, rising before midday or setting after it; may fall outside 0-24. */
	float GetSunCrossingHour(const RE::TESClimate::Timing& timing, float elevation, bool rising) const;

	/** @brief Below the minimum shadow elevation, replaces the sun's shadow direction with the one where it crossed that elevation. */
	void HoldLowSunDirection(const RE::TESClimate::Timing& timing, float hour, const RE::NiPoint3& sunDir, RE::NiPoint3& shadowDir) const;

	/** @brief Sky-local sun direction for the active path at any hour, with the night arc below the horizon. */
	RE::NiPoint3 GetSunDirectionAtHour(const RE::TESClimate::Timing& timing, float hour) const;

	void SetSunAngle();

	void SetSkyRotation(const RE::Sky* sky, RE::TESObjectCELL* cell);

	/** @brief Whether the worldspace's editor ID is in the disabled list. */
	bool IsWorldspaceDisabled(const RE::TESWorldSpace* worldspace) const;

	/** @brief Re-evaluates worldspaceDisabled; call whenever the current worldspace or the disabled list changes. */
	void UpdateWorldspaceDisabled() { worldspaceDisabled = IsWorldspaceDisabled(currentCellWorldspace); }

	/** @brief Hours at which the active sun path crosses the horizon, as { sunrise, sunset }. */
	std::pair<float, float> GetSunHorizonHours(const RE::TESClimate::Timing& timing) const;

	/** @brief The lightning flash Sky::UpdateColors adds to the directional light, 0 during weather transitions as in vanilla. */
	static float GetDirectionalFlash(const RE::Sky* sky);

	/** @brief The engine's VL fallback for weathers without a VL form, read from the fVolumetricLighting*:Display INI settings. */
	static RE::BSVolumetricLightingRenderData GetDefaultVolumetricLighting();

	/** @brief Blends the current and last weathers' night VL with the given weights, as vanilla's time-of-day VL blend does. */
	static RE::BSVolumetricLightingRenderData BlendNightVolumetricLighting(const RE::Sky* sky, float currentWeight, float lastWeight);

	void ProcessSun(const RE::Sky* sky, const RE::NiPoint3& sunDir, float sunHeight, RE::NiPoint3 dirs[], float intensities[]);

	/** @brief Fades the sun disc and glare in from the effective horizon, replacing Sun::Update's clock-based fade that runs on well after sunrise. */
	static void FadeSunAtHorizon(const RE::Sky* sky, float sunHeight);

	void ProcessMoon(const RE::Sky* sky, Caster type, RE::NiPoint3 dirs[], float intensities[]);

	/** @brief Sky-local distance of the sun disc: vanilla's own, or on the alternate path nearer at the horizon than at its peak. */
	float GetSunDistance(const RE::Sky* sky) const;

	/** @brief Sun::Update's arc as a unit direction, except that the night half runs below the horizon instead of mirrored above it. */
	static RE::NiPoint3 CalculateVanillaSunDirection(float hour, float sunrise, float sunset);

	/** @brief Angle along the alternate path's arc, 0 at sunrise and pi at sunset. */
	static float GetAlternateSunPhase(float hour, float sunrise, float sunset) { return DirectX::XM_PI * (hour - sunrise) / (sunset - sunrise); }

	/** @brief Unit direction on the alternate path, an arc tilted by sunAngle degrees from the horizon. */
	static RE::NiPoint3 CalculateAlternateSunDirection(float hour, float sunrise, float sunset, float sunAngle);

	static void SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, float distance);
};
