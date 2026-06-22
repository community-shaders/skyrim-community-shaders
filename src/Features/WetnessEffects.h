#pragma once

#include "Buffer.h"

/** @brief Adds dynamic weather-driven wetness, puddle formation, shore wetness, and raindrop effects. */
struct WetnessEffects : Feature
{
private:
	static constexpr std::string_view MOD_ID = "112739";

public:
	virtual inline std::string GetName() override { return "Wetness Effects"; }
	virtual std::string GetDisplayName() override { return T("feature.wetness_effects.name", "Wetness Effects"); }
	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "WetnessEffects"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "WETNESS_EFFECTS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }

	/** @brief Returns a summary description and list of key features for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.wetness_effects.description", "Adds realistic wetness effects including rain-based surface wetness, puddle formation, shore wetness, and dynamic raindrop effects for enhanced weather immersion."),
			{ T("feature.wetness_effects.key_feature_1", "Dynamic surface wetness based on weather conditions"),
				T("feature.wetness_effects.key_feature_2", "Realistic puddle formation and shore wetness effects"),
				T("feature.wetness_effects.key_feature_3", "Animated raindrop effects with splashes and ripples"),
				T("feature.wetness_effects.key_feature_4", "Configurable wetness intensity and weather transitions"),
				T("feature.wetness_effects.key_feature_5", "Support for skin wetness and material-specific responses") } };
	};

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		uint EnableWetnessEffects = true;
		float MaxRainWetness = 1.0f;
		float MaxPuddleWetness = 1.5f;
		float MaxShoreWetness = 1.0f;
		uint ShoreRange = 32;
		float PuddleRadius = 1.0f;
		float PuddleMaxAngle = 0.95f;
		float PuddleMinWetness = 0.85f;
		float MinRainWetness = 0.65f;
		float SkinWetness = 0.95f;
		float WeatherTransitionSpeed = 3.0f;

		// Raindrop fx settings
		uint EnableRaindropFx = true;
		uint EnableSplashes = true;
		uint EnableRipples = true;
		uint EnableVanillaRipples = false;
		float RaindropFxRange = 1000.f;
		float RaindropGridSize = 4.f;
		float RaindropInterval = 1.0f;
		float RaindropChance = 1.0f;
		float SplashesLifetime = 10.0f;
		float SplashesStrength = 1.05f;
		float SplashesMinRadius = .3f;
		float SplashesMaxRadius = .5f;
		float RippleStrength = 1.f;
		float RippleRadius = 1.f;
		float RippleBreadth = .5f;
		float RippleLifetime = .5f;
	};

	struct alignas(16) PerFrame
	{
		REX::W32::XMFLOAT4X4 OcclusionViewProj;
		float Time;
		float Raining;
		float Wetness;
		float PuddleWetness;
		Settings settings;
		uint pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	struct DebugSettings
	{
		bool EnableWetnessOverride = false;
		bool EnablePuddleOverride = false;
		bool EnableRainOverride = false;
		bool EnableIntExOverride = false;
		float2 WetnessOverride = float2(0.0f, 0.0f);
		float2 PuddleWetnessOverride = float2(0.0f, 0.0f);
		float2 RainOverride = float2(0.0f, 0.0f);
	} debugSettings;

	Settings settings;
	// Climate preset system
	enum class ClimatePreset : uint32_t
	{
		Custom = 0,
		Legacy = 1,
		NordicStandard = 2,
		ArcticTundra = 3,
		TemperateCoastal = 4,
		MonsoonExtreme = 5
	};
	struct ClimateSettings
	{
		float wetnessMultiplier;
		float puddleMultiplier;
		float transitionSpeed;
		float raindropChance;
		float raindropGridSize;
		float raindropInterval;
	};
	static constexpr ClimatePreset defaultPreset = ClimatePreset::NordicStandard;
	ClimatePreset climatePreset = defaultPreset;

	/** @brief Builds the per-frame constant buffer data including weather state and settings. */
	PerFrame GetCommonBufferData() const;

	/** @brief Updates wetness state and binds the per-frame constant buffer. */
	virtual void Prepass() override;
	/** @brief Detects Splashes of Storms mod presence for compatibility handling. */
	virtual void PostPostLoad() override;

	/** @brief Draws the ImGui settings panel for wetness effects configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	/** @brief Returns the weather analysis configuration for the debug weather analysis panel. */
	virtual WeatherAnalysisConfig GetWeatherAnalysisConfig() const override
	{
		return WeatherAnalysisConfig("Rain & Wetness Analysis", [this]() {
			this->DrawWeatherAnalysis();
		});
	}

	// Constants and utilities for rain intensity calculations
	static constexpr float MAX_RAIN_PARTICLE_DENSITY = 3.0f;

	/**
	 * @brief Extracts rain intensity from the active precipitation geometry and weather.
	 * @param precipObject The precipitation particle geometry.
	 * @param weather The current weather form.
	 * @return Normalized rain intensity in the range [0, 1].
	 */
	static float GetRainIntensity(RE::NiPointer<RE::BSGeometry> precipObject, RE::TESWeather* weather);
	/**
	 * @brief Calculates the precipitation rate in mm/hr from raindrop shader parameters.
	 * @param raindropChance Probability of a raindrop spawning per grid cell per interval.
	 * @param raindropGridSizeGameUnits Size of each raindrop grid cell in game units.
	 * @param raindropIntervalSeconds Time between raindrop spawn attempts in seconds.
	 * @param mlPerDrop Volume of each raindrop in milliliters.
	 * @return Estimated precipitation rate in mm/hr.
	 */
	float CalculatePrecipitationRate(float raindropChance, float raindropGridSizeGameUnits, float raindropIntervalSeconds, float mlPerDrop = 0.01f) const;
	/**
	 * @brief Returns the climate settings for a given preset.
	 * @param preset The climate preset to look up.
	 * @return Reference to the preset's climate settings.
	 */
	static const ClimateSettings& GetClimateSettings(ClimatePreset preset);
	/**
	 * @brief Applies a climate preset, overwriting the current wetness and raindrop settings.
	 * @param preset The climate preset to apply.
	 */
	void ApplyClimatePreset(ClimatePreset preset);
	/**
	 * @brief Checks whether the current settings exactly match a given climate preset.
	 * @param preset The climate preset to compare against.
	 * @return True if all settings match the preset values.
	 */
	bool DoesCurrentSettingsMatchPreset(ClimatePreset preset) const;
	/** @brief Detects which climate preset matches the current settings, if any. */
	void DetectCurrentPreset();

private:
	void DrawWeatherAnalysis() const;

	bool splashesOfStormsLoaded = false;

	// Weather wetness calculation result for debug display
	struct WeatherWetnessResult
	{
		float wetness = 0.0f;
		float puddleWetness = 0.0f;
	};

	WeatherWetnessResult CalculateWeatherWetness(RE::TESWeather* weather, float weatherPct, bool isCurrentWeather) const;
};
