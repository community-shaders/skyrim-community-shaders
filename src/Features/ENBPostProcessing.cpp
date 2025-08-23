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

	data.DirectLightingColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("DirectLightingColorFilter", "ENVIRONMENT");
	data.DirectLightingColorFilterAmount = settingManager.GetInterpolatedTimeOfDayValue("DirectLightingColorFilterAmount", "ENVIRONMENT");

	data.AmbientLightingIntensity = settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingIntensity", "ENVIRONMENT");
	data.AmbientLightingDesaturation = settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingDesaturation", "ENVIRONMENT");

	data.ColorPow = settingManager.GetInterpolatedTimeOfDayValue("ColorPow", "ENVIRONMENT");

	data.FogColorMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogColorMultiplier", "ENVIRONMENT");
	data.FogColorCurve = settingManager.GetInterpolatedTimeOfDayValue("FogColorCurve", "ENVIRONMENT");
	data.FogAmountMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogAmountMultiplier", "ENVIRONMENT");
	data.FogCurveMultiplier = settingManager.GetInterpolatedTimeOfDayValue("FogCurveMultiplier", "ENVIRONMENT");

	data.FogColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("FogColorFilter", "ENVIRONMENT");
	data.FogColorFilterAmount = settingManager.GetInterpolatedTimeOfDayValue("FogColorFilterAmount", "ENVIRONMENT");

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
}
