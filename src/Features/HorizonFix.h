#pragma once

#include <optional>

#include "Buffer.h"
#include "Feature.h"

/**
 * @brief Core feature enabling water rendering beyond the far clip plane for HorizonFix.
 *
 * HorizonFix (an SKSE plugin) fills the horizon gap between the farthest visible water and the
 * sky with a skirt of distant water tiles. Those tiles need shader-side support: the HORIZON_FIX
 * define in Water.hlsl folds beyond-far-plane water back onto the far plane and shades it as
 * bottomless where nothing rendered behind it.
 *
 * This feature enables that define while the HorizonFix plugin is installed and publishes the
 * plugin's far water distance so Exponential Height Fog can fog the sky out to that horizon. It
 * self-disables in PostPostLoad when the plugin is absent, so water keeps exact vanilla
 * far-clip behavior without it - and because that runs before shader cache validation, regular
 * feature validation recompiles the water shaders whenever HorizonFix is installed or removed.
 *
 * It also carries the plugin's runtime options as ordinary CS settings (saved with the CS config,
 * so presets and the Scene Manager reach them): the water skirt radius and rim quality, and the
 * plugin's horizon blend band and its angle. They take effect only while Override HorizonFix Settings is on;
 * otherwise the plugin is reset to its own HorizonFix.ini values through its exports, so a scene
 * can override the plugin in just the places it covers. CS never writes HorizonFix.ini. The
 * band is kept hidden while Exponential Height Fog is on either way - the fog already fogs the
 * sky out to the far water's horizon, which is the seam the band exists to hide.
 */
struct HorizonFix : Feature
{
	virtual inline std::string GetName() override { return "Horizon Fix"; }
	virtual std::string GetDisplayName() override { return T("feature.horizon_fix.name", "Horizon Fix"); }
	virtual inline std::string GetShortName() override { return "HorizonFix"; }
	virtual inline std::string_view GetShaderDefineName() override { return "HORIZON_FIX"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
	{
		return t == RE::BSShader::Type::Water || t == RE::BSShader::Type::Sky || t == RE::BSShader::Type::ImageSpace;
	}
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }

	/** @brief The subtitle under the feature's name in the settings header; no key features. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.horizon_fix.description", "Community Shaders integration feature for Horizon Fix"), {} };
	}

	virtual void DrawSettings() override;

	/** @brief Disables the feature when the HorizonFix plugin is not installed, and resolves its exports. */
	virtual void PostPostLoad() override;

	/** @brief Keeps the plugin in step with the settings and Exponential Height Fog every frame. */
	virtual void Prepass() override;

	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; }

	struct alignas(16) Settings
	{
		float farWaterDistance = 0.0f;  // How far the plugin's far water reaches this frame; 0 when it draws none
		float3 pad = {};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	/** @brief Publishes the plugin's far water distance; all zero unless the feature is loaded. */
	Settings GetCommonBufferData() const;

	/**
	 * @brief The plugin's options as CS settings. Used only while OverrideHorizonFixSettings is on;
	 * otherwise the plugin runs on its own HorizonFix.ini. Defaults mirror the plugin's shipped INI,
	 * and a value no loaded CS config holds starts from the installed HorizonFix.ini's.
	 */
	struct UserSettings
	{
		bool OverrideHorizonFixSettings = false;  // Use the options below instead of HorizonFix.ini; a scene can turn it on for just its own places
		bool HorizonBlendBand = true;             // Show the plugin's horizon blend band; it is hidden regardless while Exponential Height Fog is on
		float HorizonBlendDegrees = 1.5f;         // The band's angular height above and below the horizon line; 0 draws no band. Cheap to rebuild, so scenes may blend it
		int WaterSkirtRadius = 2000000;           // Game units. An integer so scenes snap it rather than blend it: every change rebuilds the whole skirt
		int WaterSkirtRimQuality = 0;             // 0-6
	} userSettings;

private:
	using FarWaterDistanceFn = float (*)();
	using SetHorizonBandEnabledFn = bool (*)(bool);
	using ResetHorizonBandFn = bool (*)();
	using SetHorizonBlendDegreesFn = void (*)(float);
	using GetHorizonBlendDegreesFn = float (*)();
	using ResetHorizonBlendDegreesFn = void (*)();
	using SetWaterSkirtRadiusFn = void (*)(float);
	using GetWaterSkirtRadiusFn = float (*)();
	using ResetWaterSkirtRadiusFn = void (*)();
	using SetWaterSkirtRimQualityFn = void (*)(int);
	using GetWaterSkirtRimQualityFn = int (*)();
	using ResetWaterSkirtRimQualityFn = void (*)();

	/** @brief True while Exponential Height Fog is loaded, turned on, and not handed off to Effects 11. */
	static bool IsHeightFogActive();

	/** @brief Hands the plugin every value that differs from what it was last given. */
	void SyncPlugin();

	/** @brief Gives each INI-backed option the loaded config does not hold the plugin's HorizonFix.ini value. */
	void ApplyIniFallbacks();

	// Exports of the companion plugin, resolved in PostPostLoad; each is null when the installed
	// version predates it. A setter only counts together with its reset (and, for an INI-backed
	// option, its getter), and its reset is non-null whenever it is.
	FarWaterDistanceFn farWaterDistanceFn = nullptr;
	SetHorizonBandEnabledFn setHorizonBandEnabledFn = nullptr;
	ResetHorizonBandFn resetHorizonBandFn = nullptr;
	SetHorizonBlendDegreesFn setHorizonBlendDegreesFn = nullptr;
	ResetHorizonBlendDegreesFn resetHorizonBlendDegreesFn = nullptr;
	SetWaterSkirtRadiusFn setWaterSkirtRadiusFn = nullptr;
	ResetWaterSkirtRadiusFn resetWaterSkirtRadiusFn = nullptr;
	SetWaterSkirtRimQualityFn setWaterSkirtRimQualityFn = nullptr;
	ResetWaterSkirtRimQualityFn resetWaterSkirtRimQualityFn = nullptr;

	// The plugin's own HorizonFix.ini values, read in PostPostLoad before anything is handed to it:
	// what an option no CS config holds starts from, and what restoring defaults returns to
	float iniBlendDegrees = 0.0f;
	int iniSkirtRadius = 0;
	int iniRimQuality = 0;

	// Whether the last loaded CS config held each option; one it did not takes the INI value
	bool blendDegreesFromConfig = false;
	bool skirtRadiusFromConfig = false;
	bool rimQualityFromConfig = false;

	// What the plugin was last given, so SyncPlugin only calls on a change. An empty value means
	// the plugin runs on its INI value, which is how it starts.
	std::optional<float> appliedBlendDegrees;
	std::optional<int> appliedSkirtRadius;
	std::optional<int> appliedRimQuality;
	std::optional<bool> appliedBandHidden;  // Empty until the first call
};
