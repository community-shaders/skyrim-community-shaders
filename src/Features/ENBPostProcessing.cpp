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
	CheckCommonData();

	auto& settingManager = SettingManager::GetSingleton();
	PerFrame data{};

	data.Enable = enableEffect;

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

	data.CloudsEdgeClamp = settingManager.GetValue<float>("CloudsEdgeClamp", "SKY");
	data.CloudsEdgeIntensity = settingManager.GetValue<float>("CloudsEdgeIntensity", "SKY");
	data.CloudsEdgeFadeRange = settingManager.GetValue<float>("CloudsEdgeFadeRange", "SKY");
	data.CloudsEdgeMoonMultiplier = settingManager.GetValue<float>("CloudsEdgeMoonMultiplier", "SKY");

	data.ColorPow = settingManager.GetInterpolatedTimeOfDayValue("ColorPow", "ENVIRONMENT");

	data.IBLMultiplicativeAmount = settingManager.GetInterpolatedTimeOfDayValue("MultiplicativeAmount", "IMAGEBASEDLIGHTING");

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
		auto& sunColor = colors[(uint)RE::TESWeather::ColorTypes::kSun];

		float3 sunColorF3 = NiToF3(sunColor);

		sunColorF3 = Intensity(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunIntensity", "SKY"));

		sunColor = F3ToNi(sunColorF3);
	}

	{
		auto& moonColor = colors[(uint)RE::TESWeather::ColorTypes::kMoonGlare];

		float3 moonColorF3 = NiToF3(moonColor);

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

void ENBPostProcessing::CheckCommonData()
{
	static Util::FrameChecker checker;
	if (checker.IsNewFrame()) {
		auto& settingManager = SettingManager::GetSingleton();
		auto& effectManager = EffectManager::GetSingleton();

		effectManager.UpdateCommonData();

		const auto& commonData = effectManager.GetCommonData();
		settingManager.SetTimeOfDayData(commonData.timeOfDay1, commonData.timeOfDay2, commonData.eInteriorFactor);
		settingManager.SetWeatherBlendFactors(
			static_cast<uint32_t>(commonData.weather[0]),
			static_cast<uint32_t>(commonData.weather[1]),
			commonData.weather[2]);

		auto ui = globals::game::ui;
		bool isMenuOpen = ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MapMenu::MENU_NAME);

		enableEffect = !isMenuOpen && settingManager.GetValue<bool>("UseEffect", "GLOBAL");
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
		globals::features::enbPostProcessing.CheckCommonData();
		if (globals::features::enbPostProcessing.enableEffect)
			globals::features::enbPostProcessing.OverrideWeather(This);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Sky_SetDirectionalAmbientColors
{
	static void thunk(ENBPostProcessing::DirectionalAmbientColors& DirectionalAmbientColors, RE::NiColor* AmbientSpecularTint, float AmbientSpecularFresnel)
	{
		globals::features::enbPostProcessing.CheckCommonData();
		if (globals::features::enbPostProcessing.enableEffect)
			globals::features::enbPostProcessing.OverrideAmbientLighting(DirectionalAmbientColors);
		func(DirectionalAmbientColors, AmbientSpecularTint, AmbientSpecularFresnel);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

struct Main_HDRTonemapBlendCinematic_Render
{
	static void thunk(RE::ImageSpaceManager* a1, RE::ImageSpaceEffect* a2, uint32_t a3, uint32_t a4, RE::ImageSpaceShaderParam* a5)
	{
		globals::features::enbPostProcessing.CheckCommonData();
		if (globals::features::enbPostProcessing.enableEffect) {
			EffectManager::GetSingleton().ExecuteEffects();
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
