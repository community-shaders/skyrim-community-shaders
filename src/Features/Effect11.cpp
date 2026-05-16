#include "Effect11.h"

#include "Effect11/D3D11StateBackup.h"
#include "Effect11/ENBHelper.h"
#include "Effect11/EffectManager.h"
#include "Effect11/MenuManager.h"
#include "Effect11/PresetManager.h"
#include "Effect11/SettingManager.h"
#include "Effect11/WeatherManager.h"

#include "CloudShadows.h"
#include "Deferred.h"
#include "IBL.h"
#include "State.h"
#include "TerrainShadows.h"
#include "Utils/D3D.h"

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

	data.VolumetricRaysDesaturation = settingManager.GetInterpolatedTimeOfDayValue("Desaturation", "GAMEVOLUMETRICRAYS");
	auto colorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "GAMEVOLUMETRICRAYS");
	data.VolumetricRaysColorFilter = { colorFilter.x, colorFilter.y, colorFilter.z };

	data.UseProceduralGradientWeights = enableEffect && settingManager.GetValue<bool>("UseProceduralGradientWeights", "SKY");
	data.ProceduralGradientWeightCurve = settingManager.GetInterpolatedTimeOfDayValue("ProceduralGradientWeightCurve", "SKY");

	data.LightSpriteIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "LIGHTSPRITE");

	data.ParticleIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "PARTICLE");
	data.ParticleLightingInfluence = settingManager.GetInterpolatedTimeOfDayValue("LightingInfluence", "PARTICLE");
	data.ParticleAmbientInfluence = settingManager.GetInterpolatedTimeOfDayValue("AmbientInfluence", "PARTICLE");
	data.ParticlePointLightingInfluence = settingManager.GetInterpolatedTimeOfDayValue("PointLightingInfluence", "PARTICLE");

	data.CalculateCloudsEdgeFromScattering = settingManager.GetValue<bool>("CalculateCloudsEdgeFromScattering", "SKYSCATTERING");
	data.EnableCloudsLightingFromMoon = settingManager.GetValue<bool>("EnableCloudsLightingFromMoon", "SKYSCATTERING");
	data.ScatteringColorHDRWeighting = settingManager.GetValue<bool>("ScatteringColorHDRWeighting", "SKYSCATTERING");
	data.SkyScatteringAtmosphereThickness = settingManager.GetInterpolatedTimeOfDayValue("AtmosphereThickness", "SKYSCATTERING");
	data.SkyScatteringHorizonRange = settingManager.GetInterpolatedTimeOfDayValue("HorizonRange", "SKYSCATTERING");
	data.SkyScatteringIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "SKYSCATTERING");
	data.SkyScatteringAmount = settingManager.GetInterpolatedTimeOfDayValue("Amount", "SKYSCATTERING");
	data.SkyScatteringDustVolume = settingManager.GetInterpolatedTimeOfDayValue("DustVolume", "SKYSCATTERING");
	data.SkyScatteringDustDensity = settingManager.GetInterpolatedTimeOfDayValue("DustDensity", "SKYSCATTERING");
	data.SkyScatteringDustDarkening = settingManager.GetInterpolatedTimeOfDayValue("DustDarkening", "SKYSCATTERING");
	data.SkyScatteringShadowAmount = settingManager.GetInterpolatedTimeOfDayValue("ShadowAmount", "SKYSCATTERING");
	data.SkyScatteringColorFromSun = settingManager.GetInterpolatedTimeOfDayValue("ColorFromSun", "SKYSCATTERING");
	auto scatteringColor = settingManager.GetInterpolatedColorTimeOfDayValue("ScatteringColor", "SKYSCATTERING");
	data.SkyScatteringColor = { scatteringColor.x, scatteringColor.y, scatteringColor.z };
	data.SkyScatteringAirGlowIntensity = settingManager.GetInterpolatedTimeOfDayValue("AirGlowIntensity", "SKYSCATTERING");
	data.SkyScatteringAirGlowRange = settingManager.GetInterpolatedTimeOfDayValue("AirGlowRange", "SKYSCATTERING");
	data.SkyScatteringSunGlowIntensity = settingManager.GetInterpolatedTimeOfDayValue("SunGlowIntensity", "SKYSCATTERING");
	data.SkyScatteringSunGlowRange = settingManager.GetInterpolatedTimeOfDayValue("SunGlowRange", "SKYSCATTERING");
	data.SkyScatteringMoonGlowAmount = settingManager.GetInterpolatedTimeOfDayValue("MoonGlowAmount", "SKYSCATTERING");
	data.SkyScatteringMoonGlowRange = settingManager.GetInterpolatedTimeOfDayValue("MoonGlowRange", "SKYSCATTERING");
	data.SkyScatteringCloudsLightingSunMinIntensity = settingManager.GetInterpolatedTimeOfDayValue("CloudsLightingSunMinIntensity", "SKYSCATTERING");
	data.SkyScatteringCloudsLightingSunMultiplier = settingManager.GetInterpolatedTimeOfDayValue("CloudsLightingSunMultiplier", "SKYSCATTERING");
	data.SkyScatteringCloudsLightingMoonIntensity = settingManager.GetInterpolatedTimeOfDayValue("CloudsLightingMoonIntensity", "SKYSCATTERING");

	data.EnableCloudsScattering = enableEffect && settingManager.GetValue<bool>("EnableCloudsScattering", "EFFECT");

	data.EnableVolumetricRays = enableEffect && settingManager.GetValue<bool>("EnableVolumetricRays", "EFFECT");
	data.VolumetricRaysIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICRAYS");
	{
		float density = std::max(0.1f, settingManager.GetInterpolatedTimeOfDayValue("Density", "VOLUMETRICRAYS"));
		data.VolumetricRaysExtinction = 0.000003f / density;
	}
	data.VolumetricRaysSkyColorAmount = settingManager.GetInterpolatedTimeOfDayValue("SkyColorAmount", "VOLUMETRICRAYS");

	data.EnableProceduralSun = enableEffect && settingManager.GetValue<bool>("EnableProceduralSun", "EFFECT");

	{
		float size = settingManager.GetValue<float>("Size", "PROCEDURALSUN");
		float edgeSoftness = settingManager.GetValue<float>("EdgeSoftness", "PROCEDURALSUN");
		float glowCurve = std::max(FLT_MIN, settingManager.GetInterpolatedTimeOfDayValue("GlowCurve", "PROCEDURALSUN"));

		float scaledSize = size * 0.04f;
		float diskSq = scaledSize * scaledSize;
		float outerSpan = std::max(1.0f - diskSq, FLT_MIN);
		float softSq = std::max(edgeSoftness * edgeSoftness, FLT_MIN);

		data.ProceduralSunDiskRadiusSq = diskSq;
		data.ProceduralSunCoronaScale = 1.0f / outerSpan;
		data.ProceduralSunDiskEdgeScale = 1.0f / (std::max(diskSq, FLT_MIN) * softSq);
		data.ProceduralSunCoronaFalloff = 100.0f / (outerSpan * glowCurve);
	}

	data.ProceduralSunGlowIntensity = settingManager.GetInterpolatedTimeOfDayValue("GlowIntensity", "PROCEDURALSUN");

	return data;
}

void Effect11::DrawSettings()
{
	MenuManager::GetSingleton().RenderImGui();
}

void Effect11::SetupResources()
{
	PresetManager::GetSingleton().Initialize();
	EffectManager::GetSingleton().Initialize();
}

void Effect11::Reset()
{
	// Reset effect state if needed
}

void Effect11::ClearShaderCache()
{
	if (volumetricRaysPS) {
		volumetricRaysPS->Release();
		volumetricRaysPS = nullptr;
	}
}

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

float3 Curve(float3 color, float power)
{
	color.x = pow(std::max(color.x, 0.0f), power);
	color.y = pow(std::max(color.y, 0.0f), power);
	color.z = pow(std::max(color.z, 0.0f), power);

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
		static auto& volumetricLighting = (*(RE::BSVolumetricLightingRenderData*)(REL::RelocationID(527719, 414629).address() - offsetof(RE::BSVolumetricLightingRenderData, red)));
		volumetricLighting.intensity *= settingManager.GetInterpolatedTimeOfDayValue("Intensity", "GAMEVOLUMETRICRAYS");
		volumetricLighting.samplingRepartition.rangeFactor *= settingManager.GetInterpolatedTimeOfDayValue("RangeFactor", "GAMEVOLUMETRICRAYS");
	}
}

void Effect11::CheckCommonData()
{
	static Util::FrameChecker checker;
	if (checker.IsNewFrame()) {
		ENBHelper::Update();

		auto& settingManager = SettingManager::GetSingleton();
		auto ui = globals::game::ui;
		bool isMenuOpen = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
		enableEffect = !isMenuOpen && settingManager.GetValue<bool>("UseEffect", "GLOBAL");

		auto& effectManager = EffectManager::GetSingleton();
		auto& weatherManager = WeatherManager::GetSingleton();

		effectManager.UpdateCommonData();

		const auto& commonData = effectManager.GetCommonData();
		settingManager.SetTimeOfDayData(commonData.timeOfDay1, commonData.timeOfDay2);

		uint32_t currentWeatherID = weatherManager.GetEffectiveWeatherID(static_cast<uint32_t>(commonData.weather[0]));
		uint32_t lastWeatherID = weatherManager.GetEffectiveWeatherID(static_cast<uint32_t>(commonData.weather[1]));
		settingManager.SetWeatherBlendFactors(currentWeatherID, lastWeatherID, commonData.weather[2]);
	}
}

void Effect11::OverridePointLightColor(float3& a_color)
{
	auto& settingManager = SettingManager::GetSingleton();

	a_color = Curve(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingCurve", "ENVIRONMENT"));
	a_color = Desaturation(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingDesaturation", "ENVIRONMENT"));
	a_color = Intensity(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingIntensity", "ENVIRONMENT"));
}

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
	static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
	{
		globals::features::effect11.ModifySky(Pass);
		func(This, Pass, RenderFlags);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

void Effect11::DrawVolumetricRays()
{
	if (!enableEffect)
		return;

	auto& settingManager = SettingManager::GetSingleton();
	if (!settingManager.GetValue<bool>("EnableVolumetricRays", "EFFECT"))
		return;

	auto& effectManager = EffectManager::GetSingleton();
	if (!effectManager.IsInitialized() || !effectManager.copyVertexShader)
		return;

	if (!volumetricRaysPS) {
		std::vector<std::pair<const char*, const char*>> defines;

		if (globals::features::cloudShadows.loaded)
			defines.push_back({ "CLOUD_SHADOWS", nullptr });

		if (globals::features::terrainShadows.loaded)
			defines.push_back({ "TERRAIN_SHADOWS", nullptr });

		if (globals::features::ibl.loaded)
			defines.push_back({ "IBL", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		volumetricRaysPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\VolumetricRaysPS.hlsl", defines, "ps_5_0"));
		if (!volumetricRaysPS)
			return;
	}

	if (!additiveBlendState) {
		D3D11_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		globals::d3d::device->CreateBlendState(&blendDesc, &additiveBlendState);
	}

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	Effect11Util::D3D11FullStateBackup stateBackup;
	stateBackup.Save(context);

	ID3D11RenderTargetView* rtv = main.RTV;
	context->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(texDesc.Width), static_cast<float>(texDesc.Height), 0, 1 };
	context->RSSetViewports(1, &viewport);

	context->OMSetBlendState(additiveBlendState, nullptr, 0xFFFFFFFF);
	context->RSSetState(effectManager.rasterizerState.get());
	context->OMSetDepthStencilState(nullptr, 0);

	UINT stride = 20;
	UINT offset = 0;
	ID3D11Buffer* vbs[] = { effectManager.quadVertexBuffer.get() };
	context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
	context->IASetInputLayout(effectManager.inputLayout.get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	context->VSSetShader(effectManager.copyVertexShader.get(), nullptr, 0);
	context->PSSetShader(volumetricRaysPS, nullptr, 0);

	auto& ibl = globals::features::ibl;
	ID3D11ShaderResourceView* srvs[16]{};
	if (ibl.loaded) {
		srvs[14] = ibl.envIBLTexture->srv.get();
		srvs[15] = ibl.skyIBLTexture->srv.get();
	}
	context->PSSetShaderResources(0, 16, srvs);

	ID3D11SamplerState* sampler = Deferred::GetSingleton()->linearSampler;
	context->PSSetSamplers(0, 1, &sampler);

	context->Draw(4, 0);

	stateBackup.Restore(context);
	stateBackup.Release();
}

void Effect11::PostPostLoad()
{
	stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x1EA, 0x178));
	if (REL::Module::IsSE())
		stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x230, 0x178));

	stl::detour_thunk<Sky_UpdateColors>(REL::RelocationID(25686, 26233));

	stl::detour_thunk<Sky_SetDirectionalAmbientColors>(REL::RelocationID(98989, 105643));
	stl::write_vfunc<0x6, BSSkyShader_SetupMaterial>(RE::VTABLE_BSSkyShader[0]);
}
