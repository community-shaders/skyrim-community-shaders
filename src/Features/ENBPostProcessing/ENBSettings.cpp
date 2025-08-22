#include "PCH.h"
#include "SettingsManager.h"

void RegisterENBSettings()
{
	auto& registry = SettingsManager::GetSingleton();

	// COLORCORRECTION settings
	registry.RegisterFloatSetting("Brightness", "COLORCORRECTION", 1.0f, 0.0f, 3.0f, false);
	registry.RegisterFloatSetting("GammaCurve", "COLORCORRECTION", 1.0f, 0.1f, 3.0f, false);

	// ADAPTATION settings
	registry.RegisterFloatSetting("AdaptationSensitivity", "ADAPTATION", 1.0f, 0.0f, 5.0f, false);
	registry.RegisterBoolSetting("ForceMinMaxValues", "ADAPTATION", false, false);
	registry.RegisterFloatSetting("AdaptationMin", "ADAPTATION", 0.0f, 0.0f, 1.0f, false);
	registry.RegisterFloatSetting("AdaptationMax", "ADAPTATION", 1.0f, 0.0f, 2.0f, false);
	registry.RegisterFloatSetting("AdaptationTime", "ADAPTATION", 1.0f, 0.1f, 10.0f, false);

	// DEPTHOFFIELD settings
	registry.RegisterFloatSetting("FocusingTime", "DEPTHOFFIELD", 1.0f, 0.1f, 10.0f, false);
	registry.RegisterFloatSetting("ApertureTime", "DEPTHOFFIELD", 1.0f, 0.1f, 10.0f, false);

	// BLOOM settings (with weather support)
	TimeOfDayValue defaultBloomAmount;
	defaultBloomAmount.Dawn = defaultBloomAmount.Sunrise = defaultBloomAmount.Day = 1.0f;
	defaultBloomAmount.Sunset = defaultBloomAmount.Dusk = defaultBloomAmount.Night = 1.0f;
	defaultBloomAmount.InteriorDay = defaultBloomAmount.InteriorNight = 1.0f;

	registry.RegisterTimeOfDaySetting("Amount", "BLOOM", defaultBloomAmount, true);

	// LENS settings (with weather support)
	TimeOfDayValue defaultLensAmount;
	defaultLensAmount.Dawn = defaultLensAmount.Sunrise = defaultLensAmount.Day = 1.0f;
	defaultLensAmount.Sunset = defaultLensAmount.Dusk = defaultLensAmount.Night = 1.0f;
	defaultLensAmount.InteriorDay = defaultLensAmount.InteriorNight = 1.0f;

	registry.RegisterTimeOfDaySetting("Amount", "LENS", defaultLensAmount, true);
}