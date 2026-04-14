#include "Effect11.h"

#include "Effect11/ENBHelper.h"
#include "Effect11/EffectManager.h"
#include "Effect11/MenuManager.h"
#include "Effect11/SettingManager.h"
#include "Effect11/WeatherManager.h"

#include "State.h"

/**
 * @brief Builds and returns the per-frame configuration used by the effect system.
 *
 * Queries current settings (including time-of-day interpolated values) and populates an Effect11::PerFrame
 * structure reflecting whether the effect and sky processing are enabled and the relevant color/cloud parameters.
 *
 * @return PerFrame Structure with:
 *  - Enable: whether the overall effect is enabled.
 *  - EnableSky: whether sky-specific modifications are enabled.
 *  - ColorPow: interpolated `ColorPow` value from the `ENVIRONMENT` group.
 *  - CloudsCurve: interpolated `CloudsCurve` value from the `SKY` group.
 *  - CloudsDesaturation: interpolated `CloudsDesaturation` value from the `SKY` group.
 *  - CloudsEdgeIntensity: `CloudsEdgeIntensity` value from the `SKY` group.
 *  - CloudsEdgeMoonMultiplier: `CloudsEdgeMoonMultiplier` value from the `SKY` group.
 */
Effect11::PerFrame Effect11::GetCommonBufferData()
{
	CheckCommonData();

	auto& settingManager = SettingManager::GetSingleton();
	PerFrame data{};

	data.Enable = enableEffect;
	data.EnableSky = enableEffect && settingManager.GetValue<bool>("Enable", "SKY");
	data.ColorPow = settingManager.GetInterpolatedTimeOfDayValue("ColorPow", "ENVIRONMENT");

	data.CloudsCurve = settingManager.GetInterpolatedTimeOfDayValue("CloudsCurve", "SKY");
	data.CloudsDesaturation = settingManager.GetInterpolatedTimeOfDayValue("CloudsDesaturation", "SKY");
	data.CloudsEdgeIntensity = settingManager.GetValue<float>("CloudsEdgeIntensity", "SKY");
	data.CloudsEdgeMoonMultiplier = settingManager.GetValue<float>("CloudsEdgeMoonMultiplier", "SKY");

	return data;
}

/**
 * @brief Renders the feature's ImGui settings UI.
 */
void Effect11::DrawSettings()
{
	MenuManager::GetSingleton().RenderImGui();
}

/**
 * @brief Initializes Effect11's resources and effect subsystem.
 *
 * Prepares runtime resources required by the effect system; should be called once during startup.
 */
void Effect11::SetupResources()
{
	// Initialize the effects system
	EffectManager::GetSingleton().Initialize();
}

/**
 * @brief Reset runtime state used by the effect to its default values.
 *
 * Restores any per-frame or cached state so the effect can start from a clean state.
 * Currently implemented as a no-op.
 */
void Effect11::Reset()
{
	// Reset effect state if needed
}

/**
 * @brief Applies time-of-day gradient scaling to the HDR sky scale when the effect and sky are enabled.
 *
 * When the feature and the "SKY" group are enabled and an ImageSpaceManager instance is available,
 * multiplies the ImageSpaceManager's HDR skyScale by a skyScaleIntensity derived from the
 * interpolated "GradientIntensity" ("SKY") setting. If the "DisableWrongSkyMath" ("SKY") setting
 * is true, the multiplier used is 0.0, otherwise it is the interpolated gradient intensity.
 */
void Effect11::Prepass()
{
	if (!enableEffect) {
		return;
	}

	auto& settingManager = SettingManager::GetSingleton();

	if (!settingManager.GetValue<bool>("Enable", "SKY")) {
		return;
	}

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	if (!imageSpaceManager) {
		return;
	}

	GET_INSTANCE_MEMBER(data, imageSpaceManager);

	float gradientIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientIntensity", "SKY");
	float skyScaleIntensity = settingManager.GetValue<bool>("DisableWrongSkyMath", "SKY") ? 0.0f : gradientIntensity;

	data.baseData.hdr.skyScale *= skyScaleIntensity;
}

/**
 * @brief Raises each RGB component of a color to the given power.
 *
 * @param color Input RGB color.
 * @param power Exponent applied to each color component.
 * @return float3 Color whose red, green, and blue components are `pow(component, power)`.
 */
float3 Curve(float3 color, float power)
{
	color.x = pow(color.x, power);
	color.y = pow(color.y, power);
	color.z = pow(color.z, power);

	return color;
}

/**
 * @brief Moves a color toward grayscale by blending each channel with its luminance.
 *
 * @param color RGB color vector to desaturate.
 * @param desaturation Blend factor in [0,1] where 0 leaves the color unchanged and 1 yields the luminance (grayscale).
 * @return float3 The desaturated RGB color.
 */
float3 Desaturation(float3 color, float desaturation)
{
	float luminance = color.Dot({ 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f });

	color.x = std::lerp(color.x, luminance, desaturation);
	color.y = std::lerp(color.y, luminance, desaturation);
	color.z = std::lerp(color.z, luminance, desaturation);

	return color;
}

/**
 * @brief Scales an RGB color by an intensity multiplier.
 *
 * @param color RGB color vector to scale.
 * @param intensity Multiplier applied to each color component.
 * @return float3 Color resulting from multiplying each component of `color` by `intensity`.
 */
float3 Intensity(float3 color, float intensity)
{
	return color * intensity;
}

/**
 * @brief Blends each color channel toward white by a given amount, then applies a per-channel multiplier.
 *
 * @param color Input RGB color.
 * @param colorFilter Per-channel multiplier applied after blending.
 * @param colorFilterAmount Blend factor toward white for each channel (0 = unchanged, 1 = fully white).
 * @return float3 Resulting color after blending toward white and applying the color filter.
 */
float3 ColorFilter(float3 color, float3 colorFilter, float colorFilterAmount)
{
	color.x = std::lerp(color.x, 1.0f, colorFilterAmount);
	color.y = std::lerp(color.y, 1.0f, colorFilterAmount);
	color.z = std::lerp(color.z, 1.0f, colorFilterAmount);

	return color * colorFilter;
}

/**
 * @brief Convert an RE::NiColor to a float3 by mapping its RGB channels.
 *
 * @param color Source NiColor whose red, green, and blue components will be used.
 * @return float3 A vector with components {red, green, blue} taken from `color`.
 */
float3 NiToF3(RE::NiColor color)
{
	return { color.red, color.green, color.blue };
}

/**
 * @brief Convert a float3 RGB vector to an RE::NiColor.
 *
 * @param color Source RGB vector where x=red, y=green, z=blue.
 * @return RE::NiColor Color with red, green, and blue set from `color`.
 */
RE::NiColor F3ToNi(float3 color)
{
	return { color.x, color.y, color.z };
}

/**
 * @brief Applies configured color and fog overrides to the given sky instance.
 *
 * Reads interpolated time-of-day and feature settings and mutates the provided RE::Sky and global volumetric lighting parameters to:
 * - adjust sunlight, fog (near/far), and effect lighting colors (curve, desaturation, color filter, intensity),
 * - scale fog power and fog range using configured multipliers,
 * - when the sky feature is enabled, modify sun, moon, stars, sun glare, sky statics, gradient horizon/sky colors, and cloud layer colors/alphas,
 * - update volumetric lighting color and sampling range.
 *
 * @param a_sky Pointer to the sky instance to modify; if null the function returns without action.
 */
void Effect11::OverrideWeather(RE::Sky* a_sky)
{
	if (!a_sky) {
		return;
	}

	auto& settingManager = SettingManager::GetSingleton();

	auto& colors = a_sky->skyColor;

	{
		auto& dirLightColor = colors[(uint)RE::TESWeather::ColorTypes::kSunlight];

		auto dirLightColorF3 = NiToF3(dirLightColor);

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		if (!imageSpaceManager) {
			return;
		}

		GET_INSTANCE_MEMBER(data, imageSpaceManager);
		float sunlightScale = std::max(data.baseData.hdr.sunlightScale, FLT_MIN);
		dirLightColorF3 *= sunlightScale;

		dirLightColorF3 = Curve(dirLightColorF3, settingManager.GetInterpolatedTimeOfDayValue("DirectLightingCurve", "ENVIRONMENT"));
		dirLightColorF3 = Desaturation(dirLightColorF3, settingManager.GetInterpolatedTimeOfDayValue("DirectLightingDesaturation", "ENVIRONMENT"));
		dirLightColorF3 = ColorFilter(dirLightColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("DirectLightingColorFilter", "ENVIRONMENT"), settingManager.GetInterpolatedTimeOfDayValue("DirectLightingColorFilterAmount", "ENVIRONMENT"));
		dirLightColorF3 = Intensity(dirLightColorF3, settingManager.GetInterpolatedTimeOfDayValue("DirectLightingIntensity", "ENVIRONMENT"));

		dirLightColorF3 /= sunlightScale;

		dirLightColor = F3ToNi(dirLightColorF3);
	}

	{
		auto& fogFarColor = colors[(uint)RE::TESWeather::ColorTypes::kFogFar];

		auto fogFarColorF3 = NiToF3(fogFarColor);

		auto fogColorCurve = settingManager.GetInterpolatedTimeOfDayValue("FogColorCurve", "ENVIRONMENT");
		auto fogColorMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogColorMultiplier", "ENVIRONMENT");

		auto fogColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("FogColorFilter", "ENVIRONMENT");
		auto fogColorFilterAmount = settingManager.GetInterpolatedTimeOfDayValue("FogColorFilterAmount", "ENVIRONMENT");

		fogFarColorF3 = Curve(fogFarColorF3, fogColorCurve);
		fogFarColorF3 = ColorFilter(fogFarColorF3, fogColorFilter, fogColorFilterAmount);
		fogFarColorF3 = Intensity(fogFarColorF3, fogColorMultiplier);

		fogFarColor = F3ToNi(fogFarColorF3);

		auto& fogNearColor = colors[(uint)RE::TESWeather::ColorTypes::kFogNear];

		auto fogNearColorF3 = NiToF3(fogNearColor);

		fogNearColorF3 = Curve(fogNearColorF3, fogColorCurve);
		fogNearColorF3 = ColorFilter(fogNearColorF3, fogColorFilter, fogColorFilterAmount);
		fogNearColorF3 = Intensity(fogNearColorF3, fogColorMultiplier);

		fogNearColor = F3ToNi(fogNearColorF3);
	}

	{
		a_sky->fogPower *= settingManager.GetInterpolatedTimeOfDayValue("FogCurveMultiplier", "ENVIRONMENT");
	}

	{
		auto fogAmountMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogAmountMultiplier", "ENVIRONMENT");
		fogAmountMultiplier = std::max(fogAmountMultiplier, FLT_MIN);

		a_sky->fogNear /= fogAmountMultiplier;
		a_sky->fogFar /= fogAmountMultiplier;
	}

	{
		auto& effectLightingColor = colors[(uint)RE::TESWeather::ColorTypes::kEffectLighting];
		auto effectLightingColorF3 = NiToF3(effectLightingColor);
		effectLightingColorF3 = Intensity(effectLightingColorF3, settingManager.GetInterpolatedTimeOfDayValue("Intensity", "PARTICLE"));
		effectLightingColor = F3ToNi(effectLightingColorF3);
	}

	const bool enableSky = enableEffect && settingManager.GetValue<bool>("Enable", "SKY");

	if (enableSky) {
		{
			auto& sunColor = colors[(uint)RE::TESWeather::ColorTypes::kSun];

			auto sunColorF3 = NiToF3(sunColor);

			sunColorF3 = Desaturation(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunDesaturation", "SKY"));
			sunColorF3 = ColorFilter(sunColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("SunColorFilter", "SKY"), 0.0f);
			sunColorF3 = Intensity(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunIntensity", "SKY"));

			sunColor = F3ToNi(sunColorF3);
		}

		{
			auto& moonColor = colors[(uint)RE::TESWeather::ColorTypes::kMoonGlare];

			auto moonColorF3 = NiToF3(moonColor);

			moonColorF3 = Desaturation(moonColorF3, settingManager.GetInterpolatedTimeOfDayValue("MoonDesaturation", "SKY"));
			moonColorF3 = ColorFilter(moonColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("MoonColorFilter", "SKY"), 0.0f);
			moonColorF3 = Intensity(moonColorF3, settingManager.GetInterpolatedTimeOfDayValue("MoonIntensity", "SKY"));

			moonColor = F3ToNi(moonColorF3);
		}

		{
			auto& starsColor = colors[(uint)RE::TESWeather::ColorTypes::kStars];

			auto starsColorF3 = NiToF3(starsColor);

			starsColorF3 = Intensity(starsColorF3, settingManager.GetInterpolatedTimeOfDayValue("StarsIntensity", "SKY"));

			starsColor = F3ToNi(starsColorF3);
		}

		{
			auto& sunGlareColor = colors[(uint)RE::TESWeather::ColorTypes::kSunGlare];

			auto sunGlareColorF3 = NiToF3(sunGlareColor);

			sunGlareColorF3 = Intensity(sunGlareColorF3, settingManager.GetInterpolatedTimeOfDayValue("GlowIntensity", "SUNGLARE"));

			sunGlareColor = F3ToNi(sunGlareColorF3);
		}

		{
			auto& skyStaticsColor = colors[(uint)RE::TESWeather::ColorTypes::kSkyStatics];

			auto skyStaticsColorF3 = NiToF3(skyStaticsColor);

			skyStaticsColorF3 = ColorFilter(skyStaticsColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "VOLUMETRICFOG"), 0.0f);
			skyStaticsColorF3 = Intensity(skyStaticsColorF3, settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICFOG"));

			skyStaticsColor = F3ToNi(skyStaticsColorF3);
		}

		float gradientIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientIntensity", "SKY");
		float gradientDesaturation = settingManager.GetInterpolatedTimeOfDayValue("GradientDesaturation", "SKY");

		{
			auto& horizonColor = colors[(uint)RE::TESWeather::ColorTypes::kHorizon];
			auto horizonColorF3 = NiToF3(horizonColor);

			horizonColorF3 = Curve(horizonColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientHorizonCurve", "SKY"));
			horizonColorF3 = ColorFilter(horizonColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("GradientHorizonColorFilter", "SKY"), 0.0f);
			horizonColorF3 *= settingManager.GetInterpolatedTimeOfDayValue("GradientHorizonIntensity", "SKY") * gradientIntensity;
			horizonColorF3 = Desaturation(horizonColorF3, gradientDesaturation);

			horizonColor = F3ToNi(horizonColorF3);
		}

		{
			auto& lowerColor = colors[(uint)RE::TESWeather::ColorTypes::kSkyLower];
			auto lowerColorF3 = NiToF3(lowerColor);

			lowerColorF3 = Curve(lowerColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientMiddleCurve", "SKY"));
			lowerColorF3 = ColorFilter(lowerColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("GradientMiddleColorFilter", "SKY"), 0.0f);
			lowerColorF3 *= settingManager.GetInterpolatedTimeOfDayValue("GradientMiddleIntensity", "SKY") * gradientIntensity;
			lowerColorF3 = Desaturation(lowerColorF3, gradientDesaturation);

			lowerColor = F3ToNi(lowerColorF3);
		}

		{
			auto& upperColor = colors[(uint)RE::TESWeather::ColorTypes::kSkyUpper];
			auto upperColorF3 = NiToF3(upperColor);

			upperColorF3 = Curve(upperColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientTopCurve", "SKY"));
			upperColorF3 = ColorFilter(upperColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("GradientTopColorFilter", "SKY"), 0.0f);
			upperColorF3 *= settingManager.GetInterpolatedTimeOfDayValue("GradientTopIntensity", "SKY") * gradientIntensity;
			upperColorF3 = Desaturation(upperColorF3, gradientDesaturation);

			upperColor = F3ToNi(upperColorF3);
		}

		if (auto clouds = a_sky->clouds) {
			auto cloudsColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("CloudsColorFilter", "SKY");
			auto cloudsIntensity = settingManager.GetInterpolatedTimeOfDayValue("CloudsIntensity", "SKY");
			auto cloudsOpacity = settingManager.GetInterpolatedTimeOfDayValue("CloudsOpacity", "SKY");

			for (uint16_t i = 0; i < clouds->numLayers; i++) {
				auto cloudColorF3 = NiToF3(clouds->colors[i]);
				cloudColorF3 *= cloudsColorFilter * cloudsIntensity;
				clouds->colors[i] = F3ToNi(cloudColorF3);
				clouds->alphas[i] *= cloudsOpacity;
			}
		}
	}

	{
		static auto& volumetricLightingRenderParams = (*(VolumetricLightingRenderParams*)REL::RelocationID(527719, 414629).address());

		auto& volumetricLightingColor = volumetricLightingRenderParams.color;
		auto volumetricLightingColorF3 = NiToF3(volumetricLightingColor);

		volumetricLightingColorF3 = Desaturation(volumetricLightingColorF3, settingManager.GetInterpolatedTimeOfDayValue("Desaturation", "GAMEVOLUMETRICRAYS"));
		volumetricLightingColorF3 = ColorFilter(volumetricLightingColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "GAMEVOLUMETRICRAYS"), 0.0f);
		volumetricLightingColorF3 *= settingManager.GetInterpolatedTimeOfDayValue("Intensity", "GAMEVOLUMETRICRAYS");

		volumetricLightingColor = F3ToNi(volumetricLightingColorF3);

		volumetricLightingRenderParams.samplingRepartition.rangeFactor *= settingManager.GetInterpolatedTimeOfDayValue("RangeFactor", "GAMEVOLUMETRICRAYS");
	}
}

/**
 * @brief Refreshes per-frame shared effect data when a new frame is detected.
 *
 * @details On the first call for a new frame, this function updates ENB helper state,
 * determines whether the effect should be enabled based on open menus and the
 * global "UseEffect" setting, updates the EffectManager's common data, and
 * forwards time-of-day and weather blend parameters to the SettingManager.
 */
void Effect11::CheckCommonData()
{
	static Util::FrameChecker checker;
	if (checker.IsNewFrame()) {
		ENBHelper::Update();

		auto& settingManager = SettingManager::GetSingleton();
		auto ui = globals::game::ui;
		bool isMenuOpen = ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
		enableEffect = !isMenuOpen && settingManager.GetValue<bool>("UseEffect", "GLOBAL");

		auto& effectManager = EffectManager::GetSingleton();
		auto& weatherManager = WeatherManager::GetSingleton();

		effectManager.UpdateCommonData();

		const auto& commonData = effectManager.GetCommonData();
		settingManager.SetTimeOfDayData(commonData.timeOfDay1, commonData.timeOfDay2, commonData.eInteriorFactor);

		uint32_t currentWeatherID = weatherManager.GetEffectiveWeatherID(static_cast<uint32_t>(commonData.weather[0]));
		uint32_t lastWeatherID = weatherManager.GetEffectiveWeatherID(static_cast<uint32_t>(commonData.weather[1]));
		settingManager.SetWeatherBlendFactors(currentWeatherID, lastWeatherID, commonData.weather[2]);
	}
}

/**
 * @brief Modifies a point-light color in place using configured environment lighting transforms.
 *
 * Applies the point-light curve, desaturation, and intensity values from the "ENVIRONMENT" settings
 * (keys: "PointLightingCurve", "PointLightingDesaturation", "PointLightingIntensity") to the input color.
 *
 * @param a_color Reference to the RGB color to be modified in place.
 */
void Effect11::OverridePointLightColor(float3& a_color)
{
	auto& settingManager = SettingManager::GetSingleton();

	a_color = Curve(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingCurve", "ENVIRONMENT"));
	a_color = Desaturation(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingDesaturation", "ENVIRONMENT"));
	a_color = Intensity(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingIntensity", "ENVIRONMENT"));
}

/**
 * @brief Modifies directional ambient colors according to environment settings.
 *
 * Applies intensity scaling to each directional ambient color and additionally applies
 * desaturation to the fourth directional side (index 3) using time-of-day interpolated values from the "ENVIRONMENT" group.
 *
 * @param DirectionalAmbientColors Directional ambient colors structure to modify in-place.
 */
void Effect11::OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors)
{
	auto& settingManager = SettingManager::GetSingleton();

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 2; j++) {
			auto& ambientLightingColor = DirectionalAmbientColors.directionalAmbientColors[i][j];

			float3 ambientLightingColorF3 = NiToF3(ambientLightingColor);

			int currentSide = i * 2 + j;
			if (currentSide == 3)
				ambientLightingColorF3 = Desaturation(ambientLightingColorF3, settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingDesaturation", "ENVIRONMENT"));

			ambientLightingColorF3 = Intensity(ambientLightingColorF3, settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingIntensity", "ENVIRONMENT"));

			ambientLightingColor = F3ToNi(ambientLightingColorF3);
		}
	}
}

struct Sky_UpdateColors
{
	/**
	 * @brief Detour invoked during sky color updates to refresh per-frame effect data and apply weather overrides when enabled.
	 *
	 * Calls the original sky update, updates effect common data for the current frame, and, if the effect is enabled, modifies the provided sky instance's colors and related parameters.
	 *
	 * @param This Pointer to the Sky instance to update.
	 * @param a_delta Time delta (in seconds) since the last update.
	 */
	static void thunk(RE::Sky* This, float a_delta)
	{
		func(This, a_delta);
		globals::features::effect11.CheckCommonData();
		if (globals::features::effect11.enableEffect)
			globals::features::effect11.OverrideWeather(This);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Sky_SetDirectionalAmbientColors
{
	/**
	 * @brief Detour handler that updates per-frame effect data and applies directional ambient lighting overrides before calling the original ambient color setup.
	 *
	 * @param DirectionalAmbientColors Reference to the directional ambient colors structure that may be modified by the effect.
	 * @param AmbientSpecularTint Pointer to the ambient specular tint color passed to the original function.
	 * @param AmbientSpecularFresnel Ambient specular fresnel value passed to the original function.
	 */
	static void thunk(Effect11::DirectionalAmbientColors& DirectionalAmbientColors, RE::NiColor* AmbientSpecularTint, float AmbientSpecularFresnel)
	{
		globals::features::effect11.CheckCommonData();
		if (globals::features::effect11.enableEffect)
			globals::features::effect11.OverrideAmbientLighting(DirectionalAmbientColors);
		func(DirectionalAmbientColors, AmbientSpecularTint, AmbientSpecularFresnel);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Main_HDRTonemapBlendCinematic_Render
{
	/**
	 * @brief Intercepts the image-space render call to run the custom effect pipeline or forward to the original renderer.
	 *
	 * When a new frame's common data is prepared, this thunk either executes the custom EffectManager pipeline if the feature is enabled and the "UseOriginalPostProcessing" setting is false, or invokes the original function to perform default image-space processing.
	 *
	 * @param a1 Pointer to the ImageSpaceManager instance handling image-space effects.
	 * @param a2 Pointer to the ImageSpaceEffect being rendered.
	 * @param a3 Render-related flags or index (engine-specific).
	 * @param a4 Render-related flags or index (engine-specific).
	 * @param a5 Optional shader parameters for the image-space effect.
	 */
	static void thunk(RE::ImageSpaceManager* a1, RE::ImageSpaceEffect* a2, uint32_t a3, uint32_t a4, RE::ImageSpaceShaderParam* a5)
	{
		globals::features::effect11.CheckCommonData();

		auto& settingManager = SettingManager::GetSingleton();
		auto& effectManager = EffectManager::GetSingleton();

		if (globals::features::effect11.enableEffect && !settingManager.GetValue<bool>("UseOriginalPostProcessing", "EFFECT")) {
			effectManager.ExecuteEffects();
		} else {
			func(a1, a2, a3, a4, a5);
		}
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

/**
 * @brief Update the global extra-shader descriptor to mark whether the current sky object is the sun.
 *
 * Checks the given render pass for a BSSkyShaderProperty and sets the State::ExtraShaderDescriptors::IsSun bit
 * in globals::state->permutationData.ExtraShaderDescriptor when the sky object type equals SO_SUN, otherwise clears it.
 *
 * @param Pass Render pass to inspect; may be null or lack a shaderProperty, in which case no change is made.
 */
void Effect11::ModifySky(RE::BSRenderPass* Pass)
{
	if (!Pass || !Pass->shaderProperty) {
		return;
	}

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	auto state = globals::state;

	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsSun);

	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN) {
		state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsSun);
	}
}

struct BSSkyShader_SetupMaterial
{
	/**
	 * @brief Adjusts sky render-pass state before delegating to the original material setup.
	 *
	 * Calls the feature-level ModifySky hook to update pass shader descriptors or other sky-specific
	 * state, then invokes the original BSShader material setup implementation.
	 *
	 * @param This Pointer to the shader instance being asked to set up material state.
	 * @param Pass Render pass containing the shader property and material data to modify.
	 * @param RenderFlags Flags controlling render-path-specific setup behavior.
	 */
	static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
	{
		globals::features::effect11.ModifySky(Pass);
		func(This, Pass, RenderFlags);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

/**
 * @brief Installs runtime hooks and call-site patches required for the effect to intercept rendering and sky update paths.
 *
 * Replaces specific HDR tonemap blend call sites to route through the effect's render thunk, detours sky color update and
 * directional ambient color setup to allow per-frame/common-data checks and weather/lighting overrides, and replaces the
 * BSSkyShader material setup virtual function to allow sky-material modifications.
 *
 * @note One of the HDR tonemap call-site patches is applied only on the Special Edition build.
 */
void Effect11::PostPostLoad()
{
	stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x1EA, 0x178));
	if (REL::Module::IsSE())
		stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x230, 0x178));

	stl::detour_thunk<Sky_UpdateColors>(REL::RelocationID(25686, 26233));

	stl::detour_thunk<Sky_SetDirectionalAmbientColors>(REL::RelocationID(98989, 105643));
	stl::write_vfunc<0x6, BSSkyShader_SetupMaterial>(RE::VTABLE_BSSkyShader[0]);
}
