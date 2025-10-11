#include "ENBPostProcessing.h"

#include "ENBPostProcessing/EffectManager.h"
#include "ENBPostProcessing/MenuManager.h"
#include "ENBPostProcessing/SettingManager.h"

void ENBPostProcessing::SaveSettings(json&)
{
}

void ENBPostProcessing::LoadSettings(json&)
{
}

void ENBPostProcessing::RestoreDefaultSettings()
{
}

ENBPostProcessing::PerFrame ENBPostProcessing::GetCommonBufferData()
{
	auto& settingManager = SettingManager::GetSingleton();
	PerFrame data{};

	data.GradientIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientIntensity", "SKY");
	data.GradientDesaturation = settingManager.GetInterpolatedTimeOfDayValue("GradientDesaturation", "SKY");
	data.GradientTopIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientTopIntensity", "SKY");
	data.GradientTopCurve = settingManager.GetInterpolatedTimeOfDayValue("GradientTopCurve", "SKY");

	data.GradientTopColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("GradientTopColorFilter", "SKY");

	data.GradientMiddleIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientMiddleIntensity", "SKY");
	data.GradientMiddleCurve = settingManager.GetInterpolatedTimeOfDayValue("GradientMiddleCurve", "SKY");

	data.GradientMiddleColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("GradientMiddleColorFilter", "SKY");

	data.GradientHorizonIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientHorizonIntensity", "SKY");
	data.GradientHorizonCurve = settingManager.GetInterpolatedTimeOfDayValue("GradientHorizonCurve", "SKY");

	data.GradientHorizonColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("GradientHorizonColorFilter", "SKY");

	data.CloudsIntensity = settingManager.GetInterpolatedTimeOfDayValue("CloudsIntensity", "SKY");
	data.CloudsCurve = settingManager.GetInterpolatedTimeOfDayValue("CloudsCurve", "SKY");
	data.CloudsDesaturation = settingManager.GetInterpolatedTimeOfDayValue("CloudsDesaturation", "SKY");
	data.CloudsOpacity = settingManager.GetInterpolatedTimeOfDayValue("CloudsOpacity", "SKY");

	data.CloudsColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("CloudsColorFilter", "SKY");

	data.DirectLightingIntensity = settingManager.GetInterpolatedTimeOfDayValue("DirectLightingIntensity", "ENVIRONMENT");
	data.DirectLightingCurve = settingManager.GetInterpolatedTimeOfDayValue("DirectLightingCurve", "ENVIRONMENT");
	data.DirectLightingDesaturation = settingManager.GetInterpolatedTimeOfDayValue("DirectLightingDesaturation", "ENVIRONMENT");

	auto dirLightColorFilterAmount = settingManager.GetInterpolatedTimeOfDayValue("DirectLightingColorFilterAmount", "ENVIRONMENT");
	auto dirLightColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("DirectLightingColorFilter", "ENVIRONMENT");
	data.DirectLightingColorFilter = (1.0f - dirLightColorFilterAmount) * float3(1.0f) + dirLightColorFilterAmount * dirLightColorFilter;

	data.AmbientLightingIntensity = settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingIntensity", "ENVIRONMENT");
	data.AmbientLightingDesaturation = settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingDesaturation", "ENVIRONMENT");

	data.ColorPow = settingManager.GetInterpolatedTimeOfDayValue("ColorPow", "ENVIRONMENT");

	data.FogColorMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogColorMultiplier", "ENVIRONMENT");
	data.FogColorCurve = settingManager.GetInterpolatedTimeOfDayValue("FogColorCurve", "ENVIRONMENT");
	data.FogAmountMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogAmountMultiplier", "ENVIRONMENT");
	data.FogCurveMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogCurveMultiplier", "ENVIRONMENT");

	auto fogColorFilterAmount = settingManager.GetInterpolatedTimeOfDayValue("FogColorFilterAmount", "ENVIRONMENT");
	auto fogColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("FogColorFilter", "ENVIRONMENT");
	data.FogColorFilter = (1.0f - fogColorFilterAmount) * float3(1.0f) + fogColorFilterAmount * fogColorFilter;

	data.IBLMultiplicativeAmount = settingManager.GetInterpolatedTimeOfDayValue("MultiplicativeAmount", "IMAGEBASEDLIGHTING");

	data.VolumetricFogIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICFOG");
	data.VolumetricFogCurve = settingManager.GetInterpolatedTimeOfDayValue("Curve", "VOLUMETRICFOG");
	data.VolumetricFogOpacity = settingManager.GetInterpolatedTimeOfDayValue("Opacity", "VOLUMETRICFOG");

	data.VolumetricFogColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "VOLUMETRICFOG");

	data.VolumetricRaysIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "GAMEVOLUMETRICRAYS");
	data.VolumetricRaysRangeFactor = settingManager.GetInterpolatedTimeOfDayValue("RangeFactor", "GAMEVOLUMETRICRAYS");
	data.VolumetricRaysDesaturation = settingManager.GetInterpolatedTimeOfDayValue("Desaturation", "GAMEVOLUMETRICRAYS");

	data.VolumetricRaysColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "GAMEVOLUMETRICRAYS");

	return data;
}

void ENBPostProcessing::DrawSettings()
{
	MenuManager::GetSingleton().RenderImGui();
}

void ENBPostProcessing::SetupResources()
{
	auto& settingManager = SettingManager::GetSingleton();
	settingManager.RegisterBoolSetting("UseEffect", "GLOBAL", true, false);

	// Create shared texture resources
	TextureManager::GetSingleton().Initialize();

	// Then initialize the effects system
	EffectManager::GetSingleton().Initialize();

	// Load registered settings
	settingManager.Load();
}

void ENBPostProcessing::Reset()
{
	// Reset effect state if needed
}

float3 Curve(float3 color, float power)
{
	color.x = pow(color.x, power);
	color.y = pow(color.y, power);
	color.z = pow(color.z, power);

	return color;
}
	
float3 Desaturation(float3 color, float desaturation)
{
	float luminance = color.Dot({ 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f });

	color.x = std::lerp(color.x, luminance, desaturation);
	color.y = std::lerp(color.y, luminance, desaturation);
	color.z = std::lerp(color.z, luminance, desaturation);

	return color;
}

float3 Intensity(float3 color, float intensity)
{
	return color * intensity;
}

float3 ColorFilter(float3 color, float3 colorFilter, float colorFilterAmount)
{
	color.x = std::lerp(color.x, 1.0f, colorFilterAmount);
	color.y = std::lerp(color.y, 1.0f, colorFilterAmount);
	color.z = std::lerp(color.z, 1.0f, colorFilterAmount);

	return color * colorFilter;
}

float3 NiToF3(RE::NiColor color)
{
	return { color.red, color.green, color.blue };
}

RE::NiColor F3ToNi(float3 color)
{
	return { color.x, color.y, color.z };
}

void ENBPostProcessing::OverrideWeather(RE::Sky* a_sky)
{
	auto& settingManager = SettingManager::GetSingleton();

	auto& colors = a_sky->skyColor;

	{
		auto& dirLightColor = colors[(uint)RE::TESWeather::ColorTypes::kSunlight];

		float3 dirLightColorF3 = NiToF3(dirLightColor);

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		dirLightColorF3 *= !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;

		dirLightColorF3 = Curve(dirLightColorF3, settingManager.GetInterpolatedTimeOfDayValue("DirectLightingCurve", "ENVIRONMENT"));
		dirLightColorF3 = Desaturation(dirLightColorF3, settingManager.GetInterpolatedTimeOfDayValue("DirectLightingDesaturation", "ENVIRONMENT"));
		dirLightColorF3 = ColorFilter(dirLightColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("DirectLightingColorFilter", "ENVIRONMENT"), settingManager.GetInterpolatedTimeOfDayValue("DirectLightingColorFilterAmount", "ENVIRONMENT"));
		dirLightColorF3 = Intensity(dirLightColorF3, settingManager.GetInterpolatedTimeOfDayValue("DirectLightingIntensity", "ENVIRONMENT"));
		
		dirLightColorF3 /= !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;

		dirLightColor = F3ToNi(dirLightColorF3);
	}

	{
		auto& fogFarColor = colors[(uint)RE::TESWeather::ColorTypes::kFogFar];

		float3 fogFarColorF3 = NiToF3(fogFarColor);

		auto fogColorCurve = settingManager.GetInterpolatedTimeOfDayValue("FogColorCurve", "ENVIRONMENT");
		auto fogColorMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogColorMultiplier", "ENVIRONMENT");

		auto fogColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("FogColorFilter", "ENVIRONMENT");
		auto fogColorFilterAmount = settingManager.GetInterpolatedTimeOfDayValue("FogColorFilterAmount", "ENVIRONMENT");

		fogFarColorF3 = Curve(fogFarColorF3, fogColorCurve);
		fogFarColorF3 = ColorFilter(fogFarColorF3, fogColorFilter, fogColorFilterAmount);
		fogFarColorF3 = Intensity(fogFarColorF3, fogColorMultiplier);

		fogFarColor = F3ToNi(fogFarColorF3);

		auto& fogNearColor = colors[(uint)RE::TESWeather::ColorTypes::kFogNear];

		float3 fogNearColorF3 = NiToF3(fogNearColor);

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
		
		a_sky->fogNear /= fogAmountMultiplier;
		a_sky->fogFar /= fogAmountMultiplier;
	}
	{
		auto skyGradientIntensity = settingManager.GetInterpolatedTimeOfDayValue("GradientIntensity", "SKY");
		auto skyGradientDesaturation = settingManager.GetInterpolatedTimeOfDayValue("GradientDesaturation", "SKY");

		{
			auto& skyHorizonColor = colors[(uint)RE::TESWeather::ColorTypes::kHorizon];

			float3 skyHorizonColorF3 = NiToF3(skyHorizonColor);

			skyHorizonColorF3 = Curve(skyHorizonColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientHorizonCurve", "SKY"));
			skyHorizonColorF3 = ColorFilter(skyHorizonColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("GradientHorizonColorFilter", "SKY"), 0.0f);
			skyHorizonColorF3 = Intensity(skyHorizonColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientHorizonIntensity", "SKY"));
			
			skyHorizonColorF3 = Desaturation(skyHorizonColorF3, skyGradientDesaturation);
			skyHorizonColorF3 = Intensity(skyHorizonColorF3, skyGradientIntensity);

			skyHorizonColor = F3ToNi(skyHorizonColorF3);
		}

		{
			auto& skyLowerColor = colors[(uint)RE::TESWeather::ColorTypes::kSkyLower];

			float3 skyLowerColorF3 = NiToF3(skyLowerColor);

			skyLowerColorF3 = Curve(skyLowerColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientMiddleCurve", "SKY"));
			skyLowerColorF3 = ColorFilter(skyLowerColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("GradientMiddleColorFilter", "SKY"), 0.0f);
			skyLowerColorF3 = Intensity(skyLowerColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientMiddleIntensity", "SKY"));

			skyLowerColorF3 = Desaturation(skyLowerColorF3, skyGradientDesaturation);
			skyLowerColorF3 = Intensity(skyLowerColorF3, skyGradientIntensity);

			skyLowerColor = F3ToNi(skyLowerColorF3);
		}

		{
			auto& skyUpperColor = colors[(uint)RE::TESWeather::ColorTypes::kSkyUpper];

			float3 skyUpperColorF3 = NiToF3(skyUpperColor);

			skyUpperColorF3 = Curve(skyUpperColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientTopCurve", "SKY"));
			skyUpperColorF3 = ColorFilter(skyUpperColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("GradientTopColorFilter", "SKY"), 0.0f);
			skyUpperColorF3 = Intensity(skyUpperColorF3, settingManager.GetInterpolatedTimeOfDayValue("GradientTopIntensity", "SKY"));
			
			skyUpperColorF3 = Desaturation(skyUpperColorF3, skyGradientDesaturation);
			skyUpperColorF3 = Intensity(skyUpperColorF3, skyGradientIntensity);

			skyUpperColor = F3ToNi(skyUpperColorF3);
		}
	}

	{
		auto& sunColor = colors[(uint)RE::TESWeather::ColorTypes::kSun];

		float3 sunColorF3 = NiToF3(sunColor);

		sunColorF3 = Desaturation(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunDesaturation", "SKY"));
		sunColorF3 = ColorFilter(sunColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("SunColorFilter", "SKY"), 0.0f);
		sunColorF3 = Intensity(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunIntensity", "SKY"));

		sunColor = F3ToNi(sunColorF3);
	}

	{
		auto& moonColor = colors[(uint)RE::TESWeather::ColorTypes::kMoonGlare];

		float3 moonColorF3 = NiToF3(moonColor);

		moonColorF3 = Desaturation(moonColorF3, settingManager.GetInterpolatedTimeOfDayValue("MoonDesaturation", "SKY"));
		moonColorF3 = ColorFilter(moonColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("MoonColorFilter", "SKY"), 0.0f);
		moonColorF3 = Intensity(moonColorF3, settingManager.GetInterpolatedTimeOfDayValue("MoonIntensity", "SKY"));

		moonColor = F3ToNi(moonColorF3);
	}

	{
		auto& starsColor = colors[(uint)RE::TESWeather::ColorTypes::kStars];

		float3 starsColorF3 = NiToF3(starsColor);
		
		starsColorF3 = Curve(starsColorF3, settingManager.GetInterpolatedTimeOfDayValue("StarsCurve", "SKY"));
		starsColorF3 = Intensity(starsColorF3, settingManager.GetInterpolatedTimeOfDayValue("StarsIntensity", "SKY"));

		starsColor = F3ToNi(starsColorF3);
	}

	{
		auto clouds = a_sky->clouds;

		auto cloudsIntensity = settingManager.GetInterpolatedTimeOfDayValue("CloudsIntensity", "SKY");
		auto cloudsCurve = settingManager.GetInterpolatedTimeOfDayValue("CloudsCurve", "SKY");
		auto cloudsDesaturation = settingManager.GetInterpolatedTimeOfDayValue("CloudsDesaturation", "SKY");
		auto cloudsOpacity = settingManager.GetInterpolatedTimeOfDayValue("CloudsOpacity", "SKY");
		auto cloudsColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("CloudsColorFilter", "SKY");

		for (int i = 0; i < RE::Clouds::kTotalLayers; i++) {
			auto& cloudColor = clouds->colors[i];

			float3 cloudColorF3 = NiToF3(cloudColor);

			cloudColorF3 = Curve(cloudColorF3, cloudsCurve);
			cloudColorF3 = Desaturation(cloudColorF3, cloudsDesaturation);
			cloudColorF3 = ColorFilter(cloudColorF3, cloudsColorFilter, 0.0f);	
			cloudColorF3 = Intensity(cloudColorF3, cloudsIntensity);
			
			cloudColor = F3ToNi(cloudColorF3);
			
			auto& cloudAlpha = clouds->colors[i];
			cloudAlpha *= cloudsOpacity;
		}
	}

	{
		auto& sunGlareColor = colors[(uint)RE::TESWeather::ColorTypes::kSunGlare];

		float3 sunGlareColorF3 = NiToF3(sunGlareColor);

		sunGlareColorF3 = Intensity(sunGlareColorF3, settingManager.GetInterpolatedTimeOfDayValue("GlowIntensity", "SUNGLARE"));

		sunGlareColor = F3ToNi(sunGlareColorF3);
	}

	{
		auto& skyStaticsColor = colors[(uint)RE::TESWeather::ColorTypes::kSkyStatics];

		float3 skyStaticsColorF3 = NiToF3(skyStaticsColor);

		skyStaticsColorF3 = Curve(skyStaticsColorF3, settingManager.GetInterpolatedTimeOfDayValue("Curve", "VOLUMETRICFOG"));
		skyStaticsColorF3 = ColorFilter(skyStaticsColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "VOLUMETRICFOG"), 0.0f);
		skyStaticsColorF3 = Intensity(skyStaticsColorF3, settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICFOG"));

		skyStaticsColor = F3ToNi(skyStaticsColorF3);
	}
}

void ENBPostProcessing::OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors)
{
	auto& settingManager = SettingManager::GetSingleton();

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 2; j++) {
			auto& ambientLightingColor = DirectionalAmbientColors.directionalAmbientColors[i][j];

			float3 ambientLightingColorF3 = NiToF3(ambientLightingColor);

			ambientLightingColorF3 = Desaturation(ambientLightingColorF3, settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingDesaturation", "ENVIRONMENT"));
			ambientLightingColorF3 = Intensity(ambientLightingColorF3, settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingIntensity", "ENVIRONMENT"));

			ambientLightingColor = F3ToNi(ambientLightingColorF3);
		}
	}
}

struct Sky_UpdateColors
{
	static void thunk(RE::Sky* This, float a_delta)
	{
		func(This, a_delta);	
		globals::features::enbPostProcessing.OverrideWeather(This);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Sky_SetDirectionalAmbientColors
{
	static void thunk(ENBPostProcessing::DirectionalAmbientColors& DirectionalAmbientColors, RE::NiColor* AmbientSpecularTint, float AmbientSpecularFresnel)
	{
		globals::features::enbPostProcessing.OverrideAmbientLighting(DirectionalAmbientColors);
		func(DirectionalAmbientColors, AmbientSpecularTint, AmbientSpecularFresnel);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Main_HDRTonemapBlendCinematic_Render
{
	static void thunk(RE::ImageSpaceManager* a1, RE::ImageSpaceEffect* a2, uint32_t a3, uint32_t a4, RE::ImageSpaceShaderParam* a5)
	{
		auto& settingManager = SettingManager::GetSingleton();
		if (settingManager.GetValue<bool>("UseEffect", "GLOBAL")) {
			auto& effectManager = EffectManager::GetSingleton();

			effectManager.UpdateCommonData();

			const auto& commonData = effectManager.GetCommonData();
			settingManager.SetTimeOfDayData(commonData.timeOfDay1, commonData.timeOfDay2, commonData.eInteriorFactor);
			settingManager.SetWeatherBlendFactors(
				static_cast<uint32_t>(commonData.weather[0]),
				static_cast<uint32_t>(commonData.weather[1]),
				commonData.weather[2]);

			effectManager.ExecuteEffects();
		} else {
			func(a1, a2, a3, a4, a5);
		}
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

void ENBPostProcessing::PostPostLoad()
{
	stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x1EA, 0x178));
	if (REL::Module::IsSE())
		stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x230, 0x178));

	stl::detour_thunk<Sky_UpdateColors>(REL::RelocationID(25686, 26233));

	stl::detour_thunk<Sky_SetDirectionalAmbientColors>(REL::RelocationID(98989, 105643));
}
