#include "Effects11.h"

#include "Effects11/D3D11StateBackup.h"
#include "Effects11/ENBHelper.h"
#include "Effects11/Editor/Effects11Editor.h"
#include "Effects11/EffectManager.h"
#include "Effects11/PresetManager.h"
#include "Effects11/SettingManager.h"

#include "CloudShadows.h"
#include "Deferred.h"
#include "Globals.h"
#include "IBL.h"
#include "PostProcessing.h"
#include "PostProcessingMode.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "ShaderCache.h"
#include "SkySync.h"
#include "State.h"
#include "TerrainShadows.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/Moon.h"

bool Effects11::HasShaderDefine(RE::BSShader::Type)
{
	return IsActive();
}

namespace
{
	/** @brief The game's volumetric lighting settings, blended by Sky::Update for the current weather and time. */
	RE::BSVolumetricLightingRenderData& GetGameVolumetricLighting()
	{
		static auto& volumetricLighting = *reinterpret_cast<RE::BSVolumetricLightingRenderData*>(REL::RelocationID(527719, 414629).address() - offsetof(RE::BSVolumetricLightingRenderData, color));
		return volumetricLighting;
	}

	/** @brief Angular half-size of a sky billboard as seen from the viewer, as tan(angle); a_fallback when it has no usable bound. */
	float GetBillboardHalfTan(const RE::NiAVObject* a_billboard, const RE::NiPoint3& a_viewer, float a_fallback)
	{
		if (!a_billboard)
			return a_fallback;

		const auto& bound = a_billboard->worldBound;
		const float distance = bound.center.GetDistance(a_viewer);
		if (bound.radius <= 0.0f || distance <= bound.radius)
			return a_fallback;

		// The bound sphere encloses the square billboard, so its half-width is radius / sqrt(2)
		return bound.radius * 0.70710678f / distance;
	}

	/** @brief A screen-sized 2D texture with an SRV, plus an RTV and/or UAV on request. */
	std::unique_ptr<Texture2D> CreateScreenTexture(uint32_t a_width, uint32_t a_height, DXGI_FORMAT a_format, bool a_renderTarget, bool a_unorderedAccess, const char* a_name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = a_format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (a_renderTarget)
			desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if (a_unorderedAccess)
			desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		auto texture = std::make_unique<Texture2D>(desc, a_name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = a_format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		texture->CreateSRV(srvDesc);

		if (a_renderTarget) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = a_format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			texture->CreateRTV(rtvDesc);
		}

		if (a_unorderedAccess) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			texture->CreateUAV(uavDesc);
		}

		return texture;
	}

	/** @brief Binds the effect manager's fullscreen quad, copy vertex shader and default raster/depth state. */
	void BindFullscreenQuad(ID3D11DeviceContext* a_context, EffectManager& a_effectManager)
	{
		UINT stride = 20;
		UINT offset = 0;
		ID3D11Buffer* vertexBuffers[] = { a_effectManager.quadVertexBuffer.get() };
		a_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
		a_context->IASetInputLayout(a_effectManager.inputLayout.get());
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		a_context->VSSetShader(a_effectManager.copyVertexShader.get(), nullptr, 0);
		a_context->RSSetState(a_effectManager.rasterizerState.get());
		a_context->OMSetDepthStencilState(nullptr, 0);
	}

	/** @brief The sky body the sun rays stream from this frame. */
	struct SunRaysLight
	{
		float3 color;
		RE::NiPoint3 direction;
		float billboardTan = 0.0f;
		float maskExponent = 0.0f;
		bool isSun = false;
	};

	/** @brief Mirrors cbuffer SunRaysData in SunRaysPS.hlsl. */
	struct SunRaysData
	{
		float2 lightUV;
		float2 uvScale;
		float2 uvMax;
		float blurFactor;
		uint32_t stepCount;
		float3 lightDirection;
		float lightBillboardTan;
		float3 raysColor;
		float maskBrightness;
		float maskExponent;
		uint32_t weightedSteps;
		uint32_t useNoise;
		float invScreenWidth;
	};
	static_assert(sizeof(SunRaysData) == 80);

	/** @brief Picks the visible sun, else the Sky Sync moon when moon rays are on; false when neither qualifies. */
	bool SelectSunRaysLight(RE::Sky* a_sky, const Effects11::PerFrame& a_perFrame, bool a_sunRays, bool a_moonRays, SunRaysLight& a_light)
	{
		// The sun disc's mask falls off much more sharply than the larger, softer moon discs
		static constexpr float sunMaskExponent = 32.0f;
		static constexpr float moonMaskExponent = 4.0f;
		const auto& skySync = globals::features::skySync;

		if (a_sunRays && EffectManager::GetSunVisibility(a_sky->sun) > 0.0f) {
			if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(a_sky->sun->sunBase->GetGeometryRuntimeData().shaderProperty.get())) {
				const auto& blend = prop->kBlendColor;
				a_light = { float3(blend.red, blend.green, blend.blue) * blend.alpha, skySync.GetCelestialDirection(a_sky, SkySync::Caster::Sun), a_perFrame.SunBillboardTan, sunMaskExponent, true };
				return true;
			}
		}

		if (!a_moonRays)
			return false;

		const auto caster = skySync.GetMoonLightCaster(a_sky);
		if (caster == SkySync::Caster::Zenith)
			return false;

		const bool masser = caster == SkySync::Caster::Masser;
		const auto moonColor = Util::Moon::GetLightColor(masser ? a_sky->masser : a_sky->secunda, masser ? Util::Moon::MasserBaseColor : Util::Moon::SecundaBaseColor,
			skySync.settings.NewMoonIntensity, skySync.settings.CrescentMoonIntensity, skySync.settings.FullMoonIntensity)
		                           .GetColor();
		a_light = { float3(moonColor.x, moonColor.y, moonColor.z), skySync.GetCelestialDirection(a_sky, caster), masser ? a_perFrame.MasserBillboardTan : a_perFrame.SecundaBillboardTan, moonMaskExponent, false };
		return true;
	}

	/** @brief Ray colour from the [RAYS] multipliers; sun rays also pick up a_skyColor, tinted by the sun. */
	float3 GetSunRaysColor(const SunRaysLight& a_light, float a_lightPeak, const float3& a_skyColor)
	{
		static constexpr float sunGain = 1.5f;
		static constexpr float skyGain = 0.5f;
		static constexpr float sunAmountScale = 3.0f;  // a sun summing to 1/3 or more lends the sky colour in full

		auto& settingManager = SettingManager::GetSingleton();
		if (!a_light.isSun)
			return a_light.color * settingManager.GetInterpolatedTimeOfDayValue("MoonRaysMultiplier", "RAYS");

		const float3 tint = (a_light.color + float3(1e-5f)) / a_lightPeak;
		const float multiplier = settingManager.GetInterpolatedTimeOfDayValue("SunRaysMultiplier", "RAYS");
		const float skyAmount = settingManager.GetInterpolatedTimeOfDayValue("SkyColorAmount", "RAYS") * multiplier;
		if (settingManager.GetValue<bool>("UseLinearMath", "RAYS"))
			return tint * multiplier + a_skyColor * skyAmount;

		const float sunAmount = std::clamp((a_light.color.x + a_light.color.y + a_light.color.z) * sunAmountScale, 0.0f, 1.0f);
		return a_light.color * tint * (sunGain * multiplier) + a_skyColor * tint * (sunAmount * skyAmount * skyGain);
	}
}

void Effects11::UpdateSkyScattering(PerFrame& a_data)
{
	auto& settingManager = SettingManager::GetSingleton();
	auto timeOfDay = [&](const char* a_key, const char* a_category = "SKYSCATTERING") {
		return settingManager.GetInterpolatedTimeOfDayValue(a_key, a_category);
	};

	auto sky = globals::game::sky;

	// Mirror the direction below the horizon while the sun is hidden (e.g. by a weather)
	if (sky && sky->sun) {
		auto direction = globals::features::skySync.GetCelestialDirection(sky, SkySync::Caster::Sun);
		if (EffectManager::GetSunVisibility(sky->sun) <= 0.0f && direction.z > 0.0f)
			direction.z = -direction.z;
		scatteringSunDirection = { direction.x, direction.y, direction.z };
	}

	const float sunHeight = scatteringSunDirection.z;
	const float sunFade = std::sqrt(std::clamp(1.0f + 2.0f * sunHeight, 0.0f, 1.0f));

	const float colorFromSun = timeOfDay("ColorFromSun");
	const auto scatteringColor = settingManager.GetInterpolatedColorTimeOfDayValue("ScatteringColor", "SKYSCATTERING");
	const float3 color = {
		(1.0f + (scatteringSunColor.x - 1.0f) * colorFromSun) * scatteringColor.x * sunFade,
		(1.0f + (scatteringSunColor.y - 1.0f) * colorFromSun) * scatteringColor.y * sunFade,
		(1.0f + (scatteringSunColor.z - 1.0f) * colorFromSun) * scatteringColor.z * sunFade
	};
	a_data.SkyScatteringColor = color;

	// Dust absorbs the channels the scattering color is weakest in, reddening the horizon band
	const float colorPeak = std::max({ color.x, color.y, color.z, 1e-6f });
	const float dustDensity = timeOfDay("DustDensity");
	const float dustTint = 2.0f * dustDensity * dustDensity;
	a_data.SkyScatteringDustTint = {
		std::max(0.0f, 1.0f - color.x / colorPeak) * dustTint,
		std::max(0.0f, 1.0f - color.y / colorPeak) * dustTint,
		std::max(0.0f, 1.0f - color.z / colorPeak) * dustTint
	};

	const float dustVolume = timeOfDay("DustVolume");
	const float horizonRange = timeOfDay("HorizonRange");
	const float atmosphereThickness = timeOfDay("AtmosphereThickness");
	const float airGlowRange = timeOfDay("AirGlowRange");
	const float sunGlowRange = timeOfDay("SunGlowRange");
	const float moonGlowRange = timeOfDay("MoonGlowRange");
	const float amount = timeOfDay("Amount");

	a_data.SkyScatteringIntensity = timeOfDay("Intensity");
	a_data.SkyScatteringShadowAmount = std::clamp(timeOfDay("ShadowAmount"), 0.0f, 1.0f);
	a_data.SkyScatteringAmount = amount * amount;
	a_data.SkyScatteringDustDarkening = timeOfDay("DustDarkening");
	a_data.SkyScatteringDustVolume = 0.02f / std::max(dustVolume * dustVolume, 1e-7f);
	a_data.SkyScatteringSunDirection = scatteringSunDirection;
	a_data.SkyScatteringSunVisibility = std::clamp((sunHeight + 0.1f) * 5.0f, 0.0f, 1.0f);
	a_data.SkyScatteringHorizonRange = 1.0f / std::max(horizonRange * horizonRange * horizonRange, 1e-6f);
	a_data.SkyScatteringAtmosphereThickness = 100.0f / std::max(atmosphereThickness * atmosphereThickness, 1e-6f);
	a_data.SkyScatteringAirGlowIntensity = timeOfDay("AirGlowIntensity");
	a_data.SkyScatteringAirGlowRange = 1.0f / std::max(airGlowRange * airGlowRange, 1e-6f);
	a_data.SkyScatteringSunGlowIntensity = timeOfDay("SunGlowIntensity");
	a_data.SkyScatteringSunGlowRange = 10.0f / std::max(sunGlowRange * sunGlowRange, 1e-6f);
	a_data.SkyScatteringMoonGlowAmount = timeOfDay("MoonGlowAmount");
	a_data.SkyScatteringMoonGlowRange = 10.0f / std::max(moonGlowRange * moonGlowRange, 1e-6f);
	a_data.SkyScatteringSunIntensity = timeOfDay("SunIntensity", "SKY");

	const float cloudsIntensity = timeOfDay("CloudsIntensity", "SKY");
	const auto cloudsColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("CloudsColorFilter", "SKY");
	a_data.CloudsIntensity = cloudsIntensity;
	a_data.CloudsColorFilter = { cloudsColorFilter.x, cloudsColorFilter.y, cloudsColorFilter.z };
	a_data.CloudsVertexAlphaBoost = timeOfDay("CloudsVertexAlphaBoost", "SKY");
	a_data.CloudsEdgeClamp = settingManager.GetValue<float>("CloudsEdgeClamp", "SKY");
	a_data.CloudsEdgeFadePower = 64.0f - 60.0f * settingManager.GetValue<float>("CloudsEdgeFadeRange", "SKY");

	a_data.CloudsLightingSunIntensity = cloudsIntensity * timeOfDay("CloudsLightingSunMultiplier") + timeOfDay("CloudsLightingSunMinIntensity");
	a_data.CloudsLightingMoonIntensity = timeOfDay("CloudsLightingMoonIntensity");
	a_data.EnableCloudsLightingFromMoon = settingManager.GetValue<bool>("EnableCloudsLightingFromMoon", "SKYSCATTERING");
	a_data.CalculateCloudsEdgeFromScattering = settingManager.GetValue<bool>("CalculateCloudsEdgeFromScattering", "SKYSCATTERING");
	a_data.CloudsLightingDesaturation = std::clamp(timeOfDay("CloudsLightingDesaturation"), -1.0f, 1.0f);
	a_data.CloudsLightingForwardScattering = std::max(0.0f, timeOfDay("CloudsLightingForwardScattering"));
	a_data.CloudsLightingDensity = std::max(0.0f, timeOfDay("CloudsLightingDensity"));

	// Fallback sun size matches the vanilla sun billboard; moons without a bound get no rim or glow
	a_data.SunBillboardTan = 425.0f / 400.0f;
	a_data.MasserBillboardTan = 0.0f;
	a_data.SecundaBillboardTan = 0.0f;
	if (sky && sky->root) {
		const auto& viewer = sky->root->world.translate;
		if (sky->sun && sky->sun->sunBase)
			a_data.SunBillboardTan = GetBillboardHalfTan(sky->sun->sunBase.get(), viewer, a_data.SunBillboardTan);
		if (sky->masser && sky->masser->moonMesh)
			a_data.MasserBillboardTan = GetBillboardHalfTan(sky->masser->moonMesh.get(), viewer, 0.0f);
		if (sky->secunda && sky->secunda->moonMesh)
			a_data.SecundaBillboardTan = GetBillboardHalfTan(sky->secunda->moonMesh.get(), viewer, 0.0f);
	}
}

Effects11::PerFrame Effects11::GetCommonBufferData()
{
	if (!IsActive())
		return {};

	if (!loaded)
		return {};

	CheckCommonData();

	if (!perFrameCacheChecker.IsNewFrame())
		return perFrameCache;

	auto& settingManager = SettingManager::GetSingleton();
	PerFrame data{};

	data.Enable = enableEffect;
	data.ColorPow = settingManager.GetInterpolatedTimeOfDayValue("ColorPow", "ENVIRONMENT");
	if (enableEffect)
		data.SkyScaleMultiplier = settingManager.GetValue<bool>("DisableWrongSkyMath", "SKY") ? 0.0f : settingManager.GetInterpolatedTimeOfDayValue("GradientIntensity", "SKY");

	data.CloudsCurve = settingManager.GetInterpolatedTimeOfDayValue("CloudsCurve", "SKY");
	data.CloudsDesaturation = settingManager.GetInterpolatedTimeOfDayValue("CloudsDesaturation", "SKY");
	data.CloudsEdgeIntensity = settingManager.GetValue<bool>("EnableCloudsEdge", "SKYSCATTERING") ? settingManager.GetValue<float>("CloudsEdgeIntensity", "SKY") : 0.0f;
	data.CloudsEdgeMoonMultiplier = settingManager.GetInterpolatedTimeOfDayValue("CloudsEdgeMoonMultiplier", "SKY");

	// StarsCurve 1..4 maps to a 0..1 blend from linear stars toward stars^4
	data.StarsCurve = std::max((settingManager.GetInterpolatedTimeOfDayValue("StarsCurve", "SKY") - 1.0f) / 3.0f, 0.0f);
	data.StarsIntensity = settingManager.GetInterpolatedTimeOfDayValue("StarsIntensity", "SKY");
	data.MoonCurve = settingManager.GetInterpolatedTimeOfDayValue("MoonCurve", "SKY");
	data.EnableAnimatedStars = settingManager.GetValue<bool>("EnableAnimatedStars", "SKY");
	data.StarsAnimationTime = settingManager.GetValue<float>("StarsAnimationTime", "SKY");
	data.StarsAnimationDensity = settingManager.GetValue<float>("StarsAnimationDensity", "SKY");
	data.StarsAnimationIntensity = settingManager.GetValue<float>("StarsAnimationIntensity", "SKY");
	data.AuroraIntensity = settingManager.GetInterpolatedTimeOfDayValue("AuroraBorealisIntensity", "SKY");
	data.AuroraCurve = settingManager.GetInterpolatedTimeOfDayValue("AuroraBorealisCurve", "SKY");
	data.FixBlackCrush = settingManager.GetValue<bool>("FixBlackCrush", "SKY");

	data.VolumetricRaysDesaturation = settingManager.GetInterpolatedTimeOfDayValue("Desaturation", "GAMEVOLUMETRICRAYS");
	auto colorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "GAMEVOLUMETRICRAYS");
	data.VolumetricRaysColorFilter = { colorFilter.x, colorFilter.y, colorFilter.z };

	data.UseProceduralGradientWeights = enableEffect && settingManager.GetValue<bool>("UseProceduralGradientWeights", "SKY");
	data.ProceduralGradientWeightCurve = settingManager.GetInterpolatedTimeOfDayValue("ProceduralGradientWeightCurve", "SKY");

	data.LightSpriteIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "LIGHTSPRITE");
	data.LightSpriteCurve = settingManager.GetInterpolatedTimeOfDayValue("Curve", "LIGHTSPRITE");

	const auto fogColorFilter = settingManager.GetInterpolatedColorTimeOfDayValue("ColorFilter", "VOLUMETRICFOG");
	data.VolumetricFogColorFilter = { fogColorFilter.x, fogColorFilter.y, fogColorFilter.z };
	data.VolumetricFogIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICFOG");
	data.VolumetricFogCurve = settingManager.GetInterpolatedTimeOfDayValue("Curve", "VOLUMETRICFOG");
	data.VolumetricFogOpacity = settingManager.GetInterpolatedTimeOfDayValue("Opacity", "VOLUMETRICFOG");
	data.VolumetricFogShadowAmount = settingManager.GetInterpolatedTimeOfDayValue("ShadowAmount", "VOLUMETRICFOG");
	data.VolumetricFogEnableLighting = settingManager.GetValue<bool>("EnableLighting", "VOLUMETRICFOG");
	data.VolumetricFogEnableShadows = settingManager.GetValue<bool>("EnableShadows", "VOLUMETRICFOG");

	data.GameVolumetricRaysDisableDithering = settingManager.GetValue<bool>("DisableGameDithering", "GAMEVOLUMETRICRAYS");
	const auto& gameVolumetricLighting = GetGameVolumetricLighting();
	// Interiors light VL from the lighting template, which the sunlight grade never touched
	data.GameVolumetricRaysLightScale = Util::IsInterior() ? float3{ 1.0f, 1.0f, 1.0f } : directLightingUngradeScale;
	data.GameVolumetricRaysCustomContribution = gameVolumetricLighting.customColor.contribution;
	data.GameVolumetricRaysCustomColor = { gameVolumetricLighting.color.red, gameVolumetricLighting.color.green, gameVolumetricLighting.color.blue };

	data.ParticleIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "PARTICLE");
	data.ParticleLightingInfluence = settingManager.GetInterpolatedTimeOfDayValue("LightingInfluence", "PARTICLE");
	data.ParticleAmbientInfluence = settingManager.GetInterpolatedTimeOfDayValue("AmbientInfluence", "PARTICLE");
	data.ParticlePointLightingInfluence = settingManager.GetInterpolatedTimeOfDayValue("PointLightingInfluence", "PARTICLE");

	data.EnableVolumetricRays = enableEffect && settingManager.GetValue<bool>("EnableVolumetricRays", "EFFECT");
	data.VolumetricRaysIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICRAYS");
	data.VolumetricRaysDensity = settingManager.GetInterpolatedTimeOfDayValue("Density", "VOLUMETRICRAYS");
	data.VolumetricRaysSkyColorAmount = settingManager.GetInterpolatedTimeOfDayValue("SkyColorAmount", "VOLUMETRICRAYS");
	// Indexed by [VOLUMETRICRAYS] Quality + 1; lower Quality values are finer.
	static constexpr uint32_t volumetricRaysSamples[4] = { 24, 16, 12, 8 };
	data.VolumetricRaysSampleCount = volumetricRaysSamples[std::clamp(static_cast<int>(std::lround(settingManager.GetValue<float>("Quality", "VOLUMETRICRAYS"))) + 1, 0, 3)];
	if (const auto sky = globals::game::sky) {
		const auto& horizonColor = sky->skyColor[(uint)RE::TESWeather::ColorTypes::kHorizon];
		data.VolumetricRaysSkyColor = { horizonColor.red, horizonColor.green, horizonColor.blue };
	}

	data.EnableCloudsScattering = enableEffect && settingManager.GetValue<bool>("EnableCloudsScattering", "EFFECT");
	UpdateSkyScattering(data);

	data.FireIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "FIRE");
	data.FireCurve = settingManager.GetInterpolatedTimeOfDayValue("Curve", "FIRE");

	data.EnableProceduralSun = enableEffect && settingManager.GetValue<bool>("EnableProceduralSun", "EFFECT");

	data.EnableWater = enableEffect && settingManager.GetValue<bool>("EnableWater", "EFFECT");
	data.WaterWavesAmplitude = settingManager.GetInterpolatedTimeOfDayValue("WavesAmplitude", "WATER");
	data.WaterMuddiness = settingManager.GetValue<float>("Muddiness", "WATER");
	data.WaterSunLightingMultiplier = settingManager.GetValue<float>("SunLightingMultiplier", "WATER");
	data.WaterSunSpecularMultiplier = settingManager.GetValue<float>("SunSpecularMultiplier", "WATER");
	data.WaterFresnelMin = settingManager.GetValue<float>("FresnelMin", "WATER");
	data.WaterFresnelMax = settingManager.GetValue<float>("FresnelMax", "WATER");
	data.WaterFresnelMultiplier = settingManager.GetValue<float>("FresnelMultiplier", "WATER");
	data.WaterReflectionAmount = settingManager.GetValue<float>("ReflectionAmount", "WATER");

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

	perFrameCache = data;
	return data;
}

void Effects11::DrawSettings()
{
	Effects11Editor::GetSingleton().DrawLauncher();
}

bool Effects11::IsUseEffectEnabled() const
{
	return loaded && SettingManager::GetSingleton().GetValue<bool>("UseEffect", "GLOBAL");
}

bool Effects11::IsPresetEnabled() const
{
	auto& manager = EffectManager::GetSingleton();
	return IsUseEffectEnabled() && manager.IsInitialized() && manager.IsPresetLoaded();
}

void Effects11::ToggleEnabled()
{
	using PostProcessingMode::Mode;
	// Effects 11 running since startup has no prior mode, so it falls back to Post Processing when installed.
	static Mode modeBeforeEffects11 = Mode::PostProcessing;
	const Mode current = PostProcessingMode::Get();
	if (current == Mode::Effects11) {
		if (!PostProcessingMode::Set(modeBeforeEffects11))
			PostProcessingMode::Set(Mode::Vanilla);
		return;
	}
	if (PostProcessingMode::Set(Mode::Effects11))
		modeBeforeEffects11 = current;
}

void Effects11::SetUseEffect(bool enabled)
{
	auto& settingManager = SettingManager::GetSingleton();
	settingManager.SetValue<bool>(settingManager.GetSettingID("UseEffect", "GLOBAL"), enabled);
}

void Effects11::SetupResources()
{
	// The active preset pack decides the preset; without one, or when it is gone, the default stays.
	auto& presetManager = PresetManager::GetSingleton();
	presetManager.DiscoverPresets();
	if (const auto presetId = UnifiedPresetCatalog::GetSingleton().GetActiveEffects11PresetId(); !presetId.empty())
		presetManager.SetActivePreset(presetId);
	EffectManager::GetSingleton().Initialize();
}

void Effects11::PostSetupResources()
{
	resourcesReady = true;
	Reset();
}

void Effects11::Reset()
{
	if (!resourcesReady)
		return;
	PostProcessingMode::ApplyPending();
	const bool enabled = IsPresetEnabled();
	if (enabled != presetActive) {
		// UseEffect can also turn on from an ini reload or preset switch; Post Processing must still yield.
		if (enabled)
			PostProcessingMode::Set(PostProcessingMode::Mode::Effects11);
		globals::shaderCache->Reload([this, enabled] { presetActive = enabled; });
	} else if (!enabled && IsUseEffectEnabled() && EffectManager::GetSingleton().IsInitialized()) {
		// UseEffect with no compiled preset has nothing to run, so fall back to the game's own post processing.
		PostProcessingMode::Set(PostProcessingMode::Mode::Vanilla);
	}
	// CheckCommonData only clears this when a hook calls it, which may not happen outside the world.
	if (!presetActive)
		enableEffect = false;
}

void Effects11::ClearShaderCache()
{
	if (raymarchVolumetricRaysPS) {
		raymarchVolumetricRaysPS->Release();
		raymarchVolumetricRaysPS = nullptr;
	}
	if (applyVolumetricRaysPS) {
		applyVolumetricRaysPS->Release();
		applyVolumetricRaysPS = nullptr;
	}
	if (blurHCS) {
		blurHCS->Release();
		blurHCS = nullptr;
	}
	if (blurVCS) {
		blurVCS->Release();
		blurVCS = nullptr;
	}
	for (auto** shader : { &sunRaysMaskPS, &sunRaysBlurPS, &sunRaysCompositePS }) {
		if (*shader) {
			(*shader)->Release();
			*shader = nullptr;
		}
	}

	if (EffectManager::GetSingleton().IsInitialized())
		EffectManager::GetSingleton().ReloadShaders();
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

	color.x = std::max(std::lerp(color.x, luminance, desaturation), 0.0f);
	color.y = std::max(std::lerp(color.y, luminance, desaturation), 0.0f);
	color.z = std::max(std::lerp(color.z, luminance, desaturation), 0.0f);

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

/** @brief A time-of-day setting moved from its interpolated value towards its Night value by nightWeight. */
static float GetNightWeightedValue(SettingManager& settingManager, std::string_view key, std::string_view category, float nightWeight)
{
	const float value = settingManager.GetInterpolatedTimeOfDayValue(key, category);
	return nightWeight > 0.0f ? std::lerp(value, settingManager.GetValue<TimeOfDayValue>(key, category)[TimeOfDayValue::Night], nightWeight) : value;
}

/** @brief Colour counterpart of GetNightWeightedValue. */
static float3 GetNightWeightedColor(SettingManager& settingManager, std::string_view key, std::string_view category, float nightWeight)
{
	const float3 value = settingManager.GetInterpolatedColorTimeOfDayValue(key, category);
	return nightWeight > 0.0f ? float3::Lerp(value, settingManager.GetValue<ColorTimeOfDayValue>(key, category)[ColorTimeOfDayValue::Night], nightWeight) : value;
}

void Effects11::OverrideWeather(RE::Sky* a_sky)
{
	if (!a_sky) {
		return;
	}

	auto& settingManager = SettingManager::GetSingleton();

	auto& colors = a_sky->skyColor;

	{
		auto& dirLightColor = colors[(uint)RE::TESWeather::ColorTypes::kSunlight];

		auto dirLightColorF3 = NiToF3(dirLightColor);
		const float3 ungradedDirLightColor = dirLightColorF3;

		// A near-zero scale would blow up the divide below, so treat it as unset
		static constexpr float minSunlightScale = 1e-3f;
		float sunlightScale = 1.0f;
		auto imageSpaceManager = globals::game::imageSpaceManager;
		if (imageSpaceManager) {
			const float rawSunlightScale = imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale;
			sunlightScale = rawSunlightScale > minSunlightScale ? rawSunlightScale : 1.0f;
		}
		dirLightColorF3 *= sunlightScale;

		// Sky Sync lights from the night sunlight colour once the sun sets, so grade it as night too
		const float nightWeight = globals::features::skySync.GetLightNightWeight();
		auto lightingValue = [&](std::string_view key) { return GetNightWeightedValue(settingManager, key, "ENVIRONMENT", nightWeight); };

		dirLightColorF3 = Curve(dirLightColorF3, lightingValue("DirectLightingCurve"));
		dirLightColorF3 = Desaturation(dirLightColorF3, lightingValue("DirectLightingDesaturation"));
		dirLightColorF3 = ColorFilter(dirLightColorF3, GetNightWeightedColor(settingManager, "DirectLightingColorFilter", "ENVIRONMENT", nightWeight), lightingValue("DirectLightingColorFilterAmount"));
		dirLightColorF3 = Intensity(dirLightColorF3, lightingValue("DirectLightingIntensity"));

		dirLightColorF3 /= sunlightScale;

		// The game lights its VL with this colour too, but ENB presets tune [GAMEVOLUMETRICRAYS] against the ungraded sunlight
		static constexpr float minGradedChannel = 1e-6f;
		auto ungradeScale = [](float a_ungraded, float a_graded) { return a_graded > minGradedChannel ? a_ungraded / a_graded : 1.0f; };
		directLightingUngradeScale = { ungradeScale(ungradedDirLightColor.x, dirLightColorF3.x), ungradeScale(ungradedDirLightColor.y, dirLightColorF3.y),
			ungradeScale(ungradedDirLightColor.z, dirLightColorF3.z) };

		dirLightColor = F3ToNi(dirLightColorF3);
	}

	// A preset handing fog to CS leaves the vanilla fog values for Exponential Height Fog's scene settings
	if (!IsHandedOff(E11Handoff::Feature::HeightFog)) {
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
			fogAmountMultiplier = std::max(fogAmountMultiplier, 1e-4f);

			a_sky->fogNear /= fogAmountMultiplier;
			a_sky->fogFar /= fogAmountMultiplier;
		}
	}

	if (enableEffect) {
		{
			auto& sunColor = colors[(uint)RE::TESWeather::ColorTypes::kSun];

			auto sunColorF3 = NiToF3(sunColor);

			sunColorF3 = Desaturation(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunDesaturation", "SKY"));
			sunColorF3 = ColorFilter(sunColorF3, settingManager.GetInterpolatedColorTimeOfDayValue("SunColorFilter", "SKY"), 0.0f);

			// Normalized only when brighter than 1: a dim sun (e.g. at dusk) keeps the scattering dim.
			// SunIntensity is left out, as it only scales the sun itself.
			const float sunColorPeak = std::max({ sunColorF3.x, sunColorF3.y, sunColorF3.z, 1.0f });
			scatteringSunColor = { std::max(sunColorF3.x, 0.0f) / sunColorPeak, std::max(sunColorF3.y, 0.0f) / sunColorPeak, std::max(sunColorF3.z, 0.0f) / sunColorPeak };

			sunColorF3 = Intensity(sunColorF3, settingManager.GetInterpolatedTimeOfDayValue("SunIntensity", "SKY"));

			sunColor = F3ToNi(sunColorF3);
		}

		{
			auto& moonColor = colors[(uint)RE::TESWeather::ColorTypes::kMoonGlare];

			// Sky Sync tints the moons from night colours once the sun sets, so grade them towards night by the same weight
			const float nightWeight = globals::features::skySync.GetMoonNightWeight();
			auto moonValue = [&](std::string_view key) { return GetNightWeightedValue(settingManager, key, "SKY", nightWeight); };

			auto moonColorF3 = NiToF3(moonColor);

			moonColorF3 = Curve(moonColorF3, moonValue("MoonCurve"));
			moonColorF3 = Desaturation(moonColorF3, moonValue("MoonDesaturation"));
			moonColorF3 = ColorFilter(moonColorF3, GetNightWeightedColor(settingManager, "MoonColorFilter", "SKY", nightWeight), 0.0f);
			moonColorF3 = Intensity(moonColorF3, moonValue("MoonIntensity"));

			moonColor = F3ToNi(moonColorF3);
		}

		{
			auto& sunGlareColor = colors[(uint)RE::TESWeather::ColorTypes::kSunGlare];

			auto sunGlareColorF3 = NiToF3(sunGlareColor);

			sunGlareColorF3 = Intensity(sunGlareColorF3, settingManager.GetInterpolatedTimeOfDayValue("GlowIntensity", "SUNGLARE"));

			sunGlareColor = F3ToNi(sunGlareColorF3);
		}

		if (settingManager.GetValue<bool>("EnableWater", "EFFECT")) {
			auto& waterColor = colors[(uint)RE::TESWeather::ColorTypes::kWaterMultiplier];

			auto waterColorF3 = NiToF3(waterColor);

			waterColorF3 = Intensity(waterColorF3, settingManager.GetInterpolatedTimeOfDayValue("Brightness", "WATER"));

			waterColor = F3ToNi(waterColorF3);
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

		// CloudsIntensity and CloudsColorFilter are applied by the sky shader, after CloudsCurve
		if (auto clouds = a_sky->clouds) {
			auto cloudsOpacity = settingManager.GetInterpolatedTimeOfDayValue("CloudsOpacity", "SKY");

			for (uint16_t i = 0; i < clouds->numLayers; i++)
				clouds->alphas[i] *= cloudsOpacity;
		}
	}

	{
		auto& volumetricLighting = GetGameVolumetricLighting();
		volumetricLighting.intensity *= settingManager.GetInterpolatedTimeOfDayValue("Intensity", "GAMEVOLUMETRICRAYS");
		volumetricLighting.samplingRepartition.rangeFactor *= settingManager.GetInterpolatedTimeOfDayValue("RangeFactor", "GAMEVOLUMETRICRAYS");
	}
}

void Effects11::CheckCommonData()
{
	if (!IsActive()) {
		enableEffect = false;
		return;
	}

	static Util::FrameChecker checker;
	if (checker.IsNewFrame()) {
		ENBHelper::Update();

		auto& settingManager = SettingManager::GetSingleton();
		auto& effectManager = EffectManager::GetSingleton();

		enableEffect = !globals::state->IsFullScreenMenuOpen() && globals::shaderCache->IsEnabled() && effectManager.IsPresetLoaded();

		effectManager.UpdateCommonData();

		const auto& commonData = effectManager.GetCommonData();
		settingManager.SetTimeOfDayData(commonData.timeOfDay1, commonData.timeOfDay2);
		settingManager.SetWeatherBlendFactors(effectManager.currentWeatherID, effectManager.previousWeatherID, commonData.weather[2]);

		pointLighting.curve = settingManager.GetInterpolatedTimeOfDayValue("PointLightingCurve", "ENVIRONMENT");
		pointLighting.desaturation = settingManager.GetInterpolatedTimeOfDayValue("PointLightingDesaturation", "ENVIRONMENT");
		pointLighting.intensity = settingManager.GetInterpolatedTimeOfDayValue("PointLightingIntensity", "ENVIRONMENT");
	}
}

void Effects11::OverridePointLightColor(float3& a_color)
{
	a_color = Curve(a_color, pointLighting.curve);
	a_color = Desaturation(a_color, pointLighting.desaturation);
	a_color = Intensity(a_color, pointLighting.intensity);
}

void Effects11::OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors)
{
	auto& settingManager = SettingManager::GetSingleton();
	const float desaturation = settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingDesaturation", "ENVIRONMENT");
	const float intensity = settingManager.GetInterpolatedTimeOfDayValue("AmbientLightingIntensity", "ENVIRONMENT");

	auto& colors = DirectionalAmbientColors.directionalAmbientColors;
	// ENB desaturates only side 3 (Y-), not the whole cube
	auto& desaturatedSide = colors[1][1];
	desaturatedSide = F3ToNi(Desaturation(NiToF3(desaturatedSide), desaturation));

	for (auto& axis : colors) {
		for (auto& ambientLightingColor : axis)
			ambientLightingColor = F3ToNi(Intensity(NiToF3(ambientLightingColor), intensity));
	}
}

void Effects11::OnSkyUpdateColors(RE::Sky* a_sky)
{
	CheckCommonData();
	if (enableEffect)
		OverrideWeather(a_sky);
}

bool Effects11::WantsTonemapOwnership()
{
	CheckCommonData();

	// The initialized check must be part of ownership, not just of rendering: if it were only
	// checked at render time, the arbiter would still report Effects11 as the owner while the
	// vanilla pass ran, having already stripped Post Processing's tonemap flag and skipped its
	// pipeline for that frame.
	auto& effectManager = EffectManager::GetSingleton();
	if (!effectManager.IsInitialized() || !effectManager.IsPresetLoaded())
		return false;

	// Post Processing tonemapping and Effects 11 tonemapping cannot share the frame; when PP
	// wants ownership, this preset yields (avoids a black screen).
	auto& postProcessing = globals::features::postProcessing;
	if (postProcessing.loaded && postProcessing.WantsTonemapOwnership())
		return false;

	return enableEffect && !SettingManager::GetSingleton().GetValue<bool>("UseOriginalPostProcessing", "EFFECT");
}

bool Effects11::RenderTonemap(RE::RENDER_TARGET a_input, RE::RENDER_TARGET a_output)
{
	auto& effectManager = EffectManager::GetSingleton();
	if (!effectManager.IsInitialized())
		return false;

	auto& renderTargets = globals::game::renderer->GetRuntimeData().renderTargets;
	// Only report replacement after the effect chain actually wrote the output.
	if (effectManager.ExecuteEffects(renderTargets[a_input], renderTargets[a_output])) {
		tonemapReplacedFrame = globals::state->frameCount;
		return true;
	}
	return false;
}

bool Effects11::ReplacedTonemapperThisFrame() const
{
	return tonemapReplacedFrame == globals::state->frameCount;
}

void Effects11::ModifySky(RE::BSRenderPass* Pass)
{
	// State::UpdateSkyShaderPermutation ran first and already flagged both the sun disc and its
	// glare; only narrow that to the disc when a preset is actually driving the procedural sun
	if (!enableEffect)
		return;

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

void Effects11::DrawVolumetricRays()
{
	if (!enableEffect)
		return;

	if (Util::IsInterior())
		return;

	auto sky = globals::game::sky;
	if (!sky || sky->flags.any(RE::Sky::Flags::kHideSky))
		return;

	if (globals::state->IsFullScreenMenuOpen())
		return;

	// The apply pass lights the rays with SunColor, which is premultiplied by the sun's visibility,
	// so with the sun faded out (overcast, rain, fog, night) the passes would only add black
	auto& settingManager = SettingManager::GetSingleton();
	if (!settingManager.GetValue<bool>("EnableVolumetricRays", "EFFECT") || EffectManager::GetSunVisibility(sky->sun) <= 0.0f)
		return;

	auto& effectManager = EffectManager::GetSingleton();
	if (!effectManager.IsInitialized() || !effectManager.copyVertexShader)
		return;

	if (!raymarchVolumetricRaysPS) {
		std::vector<std::pair<const char*, const char*>> defines;
		if (globals::features::cloudShadows.loaded)
			defines.push_back({ "CLOUD_SHADOWS", nullptr });
		if (globals::features::terrainShadows.loaded)
			defines.push_back({ "TERRAIN_SHADOWS", nullptr });

		raymarchVolumetricRaysPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\RaymarchVolumetricRaysPS.hlsl", defines, "ps_5_0"));
		if (!raymarchVolumetricRaysPS)
			return;
	}

	if (!applyVolumetricRaysPS) {
		applyVolumetricRaysPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\ApplyVolumetricRaysPS.hlsl", {}, "ps_5_0"));
		if (!applyVolumetricRaysPS)
			return;
	}

	if (!blurHCS) {
		blurHCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\BlurVolumetricRaysCS.hlsl", { { "HORIZONTAL", nullptr } }, "cs_5_0"));
		if (!blurHCS)
			return;
	}

	if (!blurVCS) {
		blurVCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\BlurVolumetricRaysCS.hlsl", {}, "cs_5_0"));
		if (!blurVCS)
			return;
	}

	if (!EnsureAdditiveBlendState())
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC mainTexDesc{};
	main.texture->GetDesc(&mainTexDesc);
	float2 resolution = { static_cast<float>(mainTexDesc.Width), static_cast<float>(mainTexDesc.Height) };
	resolution = Util::ConvertToDynamic(resolution);
	uint32_t dynWidth = static_cast<uint32_t>(resolution.x);
	uint32_t dynHeight = static_cast<uint32_t>(resolution.y);

	// Raymarch + blurs run at half resolution; the apply pass upsamples bilaterally.
	const uint32_t halfTexWidth = (mainTexDesc.Width + 1) / 2;
	const uint32_t halfTexHeight = (mainTexDesc.Height + 1) / 2;
	const uint32_t halfDynWidth = (dynWidth + 1) / 2;
	const uint32_t halfDynHeight = (dynHeight + 1) / 2;

	if (!vlTexA || vlTexA->desc.Width != halfTexWidth || vlTexA->desc.Height != halfTexHeight) {
		vlTexA = CreateScreenTexture(halfTexWidth, halfTexHeight, DXGI_FORMAT_R16_FLOAT, true, true, "Effects11::VLTexA");
		vlTexB = CreateScreenTexture(halfTexWidth, halfTexHeight, DXGI_FORMAT_R16_FLOAT, false, true, "Effects11::VLTexB");
		vlDepthHalf = CreateScreenTexture(halfTexWidth, halfTexHeight, DXGI_FORMAT_R32_FLOAT, true, false, "Effects11::VLDepthHalf");
	}

	if (!vlBlurCB)
		vlBlurCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc(16), "Effects11::VLBlurCB");

	Effects11Util::D3D11ScopedPostFxBackup stateBackup;
	stateBackup.Save(context);

	ID3D11SamplerState* sampler = Deferred::GetSingleton()->linearSampler;
	D3D11_VIEWPORT viewport{ 0, 0, resolution.x, resolution.y, 0, 1 };
	D3D11_VIEWPORT halfViewport{ 0, 0, static_cast<float>(halfDynWidth), static_cast<float>(halfDynHeight), 0, 1 };

	auto* profiler = globals::profiler;

	BindFullscreenQuad(context, effectManager);

	// Pass 1: Raymarch shadow + depth → half-res textures (MRT)
	{
		profiler->BeginPass("Effects11::VolumetricRays Pass 0");

		ID3D11RenderTargetView* rtvs[2] = { vlTexA->rtv.get(), vlDepthHalf->rtv.get() };
		context->OMSetRenderTargets(2, rtvs, nullptr);
		context->RSSetViewports(1, &halfViewport);

		context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		context->PSSetShader(raymarchVolumetricRaysPS, nullptr, 0);
		context->PSSetSamplers(0, 1, &sampler);

		context->Draw(4, 0);

		ID3D11RenderTargetView* nullRTVs[2] = { nullptr, nullptr };
		context->OMSetRenderTargets(2, nullRTVs, nullptr);

		profiler->EndPass();
	}

	// Blur setup
	struct VLData
	{
		int32_t screenX, screenY, screenXMin1, screenYMin1;
	};
	VLData vlData = { static_cast<int32_t>(halfDynWidth), static_cast<int32_t>(halfDynHeight), static_cast<int32_t>(halfDynWidth) - 1, static_cast<int32_t>(halfDynHeight) - 1 };
	vlBlurCB->Update(vlData);

	static constexpr uint32_t tgDim = 256;
	static constexpr uint32_t blurWindow = 12;
	static constexpr uint32_t effectiveGroupSize = tgDim - blurWindow * 2;

	// Pass 2: Blur horizontal (texA → texB)
	{
		profiler->BeginPass("Effects11::VolumetricRays Pass 1");
		context->CSSetShader(blurHCS, nullptr, 0);

		ID3D11ShaderResourceView* csSRVs[2] = { vlTexA->srv.get(), vlDepthHalf->srv.get() };
		context->CSSetShaderResources(0, 2, csSRVs);

		ID3D11UnorderedAccessView* csUAVs[1] = { vlTexB->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, csUAVs, nullptr);

		ID3D11Buffer* csCBs[2] = { nullptr, vlBlurCB->CB() };
		context->CSSetConstantBuffers(0, 2, csCBs);

		uint32_t groupsX = (halfDynWidth + effectiveGroupSize - 1) / effectiveGroupSize;
		context->Dispatch(groupsX, halfDynHeight, 1);

		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		profiler->EndPass();
	}

	// Pass 3: Blur vertical (texB → texA)
	{
		profiler->BeginPass("Effects11::VolumetricRays Pass 2");
		context->CSSetShader(blurVCS, nullptr, 0);

		ID3D11ShaderResourceView* csSRVs[2] = { vlTexB->srv.get(), vlDepthHalf->srv.get() };
		context->CSSetShaderResources(0, 2, csSRVs);

		ID3D11UnorderedAccessView* csUAVs[1] = { vlTexA->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, csUAVs, nullptr);

		uint32_t groupsY = (halfDynHeight + effectiveGroupSize - 1) / effectiveGroupSize;
		context->Dispatch(halfDynWidth, groupsY, 1);

		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
		profiler->EndPass();
	}

	// Pass 4: Apply blurred shadow with color → main RT (additive)
	{
		profiler->BeginPass("Effects11::VolumetricRays Pass 3");
		ID3D11RenderTargetView* rtv = main.RTV;
		context->OMSetRenderTargets(1, &rtv, nullptr);
		context->RSSetViewports(1, &viewport);

		context->OMSetBlendState(additiveBlendState.get(), nullptr, 0xFFFFFFFF);
		context->PSSetShader(applyVolumetricRaysPS, nullptr, 0);

		ID3D11ShaderResourceView* srvs[2] = { vlTexA->srv.get(), vlDepthHalf->srv.get() };
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 1, &sampler);

		// Half-res dimensions for the bilateral upsample.
		ID3D11Buffer* psCB = vlBlurCB->CB();
		context->PSSetConstantBuffers(1, 1, &psCB);

		context->Draw(4, 0);
		profiler->EndPass();
	}

	stateBackup.Restore(context);
	stateBackup.Release();
}

bool Effects11::EnsureAdditiveBlendState()
{
	if (additiveBlendState)
		return true;

	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
	if (FAILED(globals::d3d::device->CreateBlendState(&blendDesc, additiveBlendState.put())))
		return false;
	Util::SetResourceName(additiveBlendState.get(), "Effects11::AdditiveBlendState");
	return true;
}

bool Effects11::EnsureSunRaysResources(uint32_t a_width, uint32_t a_height)
{
	auto compile = [](ID3D11PixelShader*& a_shader, const char* a_mode, bool a_cloudShadows) {
		if (a_shader)
			return true;
		std::vector<std::pair<const char*, const char*>> defines = { { a_mode, nullptr } };
		if (a_cloudShadows)
			defines.push_back({ "CLOUD_SHADOWS", nullptr });
		a_shader = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\SunRaysPS.hlsl", defines, "ps_5_0"));
		return a_shader != nullptr;
	};
	if (!compile(sunRaysMaskPS, "MASK", globals::features::cloudShadows.loaded) ||
		!compile(sunRaysBlurPS, "BLUR", false) ||
		!compile(sunRaysCompositePS, "COMPOSITE", false))
		return false;

	if (!EnsureAdditiveBlendState())
		return false;

	if (!sunRaysTexA || sunRaysTexA->desc.Width != a_width || sunRaysTexA->desc.Height != a_height) {
		sunRaysTexA = CreateScreenTexture(a_width, a_height, DXGI_FORMAT_R16_FLOAT, true, false, "Effects11::SunRaysTexA");
		sunRaysTexB = CreateScreenTexture(a_width, a_height, DXGI_FORMAT_R16_FLOAT, true, false, "Effects11::SunRaysTexB");
	}

	if (!sunRaysCB)
		sunRaysCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<SunRaysData>(), "Effects11::SunRaysCB");

	return true;
}

void Effects11::DrawSunRays()
{
	if (!enableEffect || Util::IsInterior())
		return;

	auto sky = globals::game::sky;
	if (!sky || sky->flags.any(RE::Sky::Flags::kHideSky))
		return;

	if (globals::state->IsFullScreenMenuOpen())
		return;

	auto& settingManager = SettingManager::GetSingleton();
	const bool sunRays = settingManager.GetValue<bool>("EnableSunRays", "EFFECT");
	const bool moonRays = settingManager.GetValue<bool>("EnableMoonRays", "EFFECT");
	if (!sunRays && !moonRays)
		return;

	auto& effectManager = EffectManager::GetSingleton();
	if (!effectManager.IsInitialized() || !effectManager.copyVertexShader)
		return;

	const PerFrame perFrame = GetCommonBufferData();
	SunRaysLight light;
	if (!SelectSunRaysLight(sky, perFrame, sunRays, moonRays, light))
		return;

	const float lightPeak = std::max({ light.color.x, light.color.y, light.color.z });
	if (lightPeak <= 1e-4f || light.billboardTan <= 0.0f)
		return;

	const auto viewProj = globals::game::frameBufferCached.GetCameraViewProj().Transpose();
	const auto clip = DirectX::SimpleMath::Vector4::Transform(DirectX::SimpleMath::Vector4(light.direction.x, light.direction.y, light.direction.z, 0.0f), viewProj);
	if (clip.w <= 1e-6f)
		return;

	// Past this the blur no longer reaches the screen from the light's projected position
	static constexpr float maxLightNdcOffset = 2.5f;
	const float2 lightNdc = { clip.x / clip.w, clip.y / clip.w };
	if (std::max(std::abs(lightNdc.x), std::abs(lightNdc.y)) >= maxLightNdcOffset)
		return;

	auto context = globals::d3d::context;
	auto& main = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC mainTexDesc{};
	main.texture->GetDesc(&mainTexDesc);
	const float2 resolution = Util::ConvertToDynamic(float2{ static_cast<float>(mainTexDesc.Width), static_cast<float>(mainTexDesc.Height) });
	const uint32_t halfTexWidth = (mainTexDesc.Width + 1) / 2;
	const uint32_t halfTexHeight = (mainTexDesc.Height + 1) / 2;
	const uint32_t halfDynWidth = (static_cast<uint32_t>(resolution.x) + 1) / 2;
	const uint32_t halfDynHeight = (static_cast<uint32_t>(resolution.y) + 1) / 2;

	if (!EnsureSunRaysResources(halfTexWidth, halfTexHeight))
		return;

	// Step counts are indexed by [RAYS] Quality + 2, blur factors by Type, or MoonType for moon rays
	static constexpr uint32_t blurSteps[3][5] = { { 7, 6, 5, 4, 4 }, { 7, 6, 6, 5, 4 }, { 7, 7, 6, 5, 4 } };
	static constexpr uint32_t compositeSteps[5] = { 8, 8, 7, 6, 4 };
	static constexpr float shortBlur[5] = { 0.15f, 0.3f, 0.6f, 0.3f, 0.9f };
	static constexpr float longBlur[5] = { 0.4f, 0.7f, 0.6f, 1.0f, 0.9f };
	const int qualityIndex = std::clamp(static_cast<int>(std::lround(settingManager.GetValue<float>("Quality", "RAYS"))) + 2, 0, 4);
	const int type = std::clamp(static_cast<int>(std::lround(settingManager.GetValue<float>(light.isSun ? "Type" : "MoonType", "RAYS"))), 0, 4);

	SunRaysData data{};
	data.lightUV = { lightNdc.x * 0.5f + 0.5f, 0.5f - lightNdc.y * 0.5f };
	data.uvScale = { static_cast<float>(halfDynWidth) / halfTexWidth, static_cast<float>(halfDynHeight) / halfTexHeight };
	data.uvMax = { (halfDynWidth - 0.5f) / halfTexWidth, (halfDynHeight - 0.5f) / halfTexHeight };
	data.lightDirection = { light.direction.x, light.direction.y, light.direction.z };
	data.lightBillboardTan = light.billboardTan;
	data.raysColor = GetSunRaysColor(light, lightPeak, perFrame.VolumetricRaysSkyColor);
	data.maskBrightness = lightPeak;
	data.maskExponent = light.maskExponent;
	data.invScreenWidth = 1.0f / std::max(resolution.x, 1.0f);

	Effects11Util::D3D11ScopedPostFxBackup stateBackup;
	stateBackup.Save(context);

	ID3D11SamplerState* sampler = Deferred::GetSingleton()->linearSampler;
	const D3D11_VIEWPORT viewport{ 0, 0, resolution.x, resolution.y, 0, 1 };
	const D3D11_VIEWPORT halfViewport{ 0, 0, static_cast<float>(halfDynWidth), static_cast<float>(halfDynHeight), 0, 1 };

	BindFullscreenQuad(context, effectManager);
	context->PSSetSamplers(0, 1, &sampler);

	auto* profiler = globals::profiler;
	auto drawPass = [&](ID3D11PixelShader* a_shader, ID3D11RenderTargetView* a_rtv, ID3D11ShaderResourceView* a_srv, const D3D11_VIEWPORT& a_viewport, ID3D11BlendState* a_blendState, const char* a_name) {
		profiler->BeginPass(a_name);
		sunRaysCB->Update(data);
		ID3D11Buffer* cb = sunRaysCB->CB();
		context->PSSetConstantBuffers(1, 1, &cb);
		// Unbind the previous pass's input before it becomes this pass's target
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->OMSetRenderTargets(1, &a_rtv, nullptr);
		context->RSSetViewports(1, &a_viewport);
		context->OMSetBlendState(a_blendState, nullptr, 0xFFFFFFFF);
		context->PSSetShader(a_shader, nullptr, 0);
		context->PSSetShaderResources(0, 1, &a_srv);
		context->Draw(4, 0);
		profiler->EndPass();
	};

	drawPass(sunRaysMaskPS, sunRaysTexA->rtv.get(), nullptr, halfViewport, nullptr, "Effects11::SunRays Mask");

	data.blurFactor = shortBlur[type];
	data.stepCount = blurSteps[0][qualityIndex];
	drawPass(sunRaysBlurPS, sunRaysTexB->rtv.get(), sunRaysTexA->srv.get(), halfViewport, nullptr, "Effects11::SunRays Blur 1");

	data.blurFactor = longBlur[type];
	data.stepCount = blurSteps[1][qualityIndex];
	data.weightedSteps = 1;
	data.useNoise = 1;
	drawPass(sunRaysBlurPS, sunRaysTexA->rtv.get(), sunRaysTexB->srv.get(), halfViewport, nullptr, "Effects11::SunRays Blur 2");

	data.blurFactor = shortBlur[type];
	data.stepCount = blurSteps[2][qualityIndex];
	data.weightedSteps = 0;
	drawPass(sunRaysBlurPS, sunRaysTexB->rtv.get(), sunRaysTexA->srv.get(), halfViewport, nullptr, "Effects11::SunRays Blur 3");

	data.stepCount = compositeSteps[qualityIndex];
	data.useNoise = qualityIndex == 4;
	drawPass(sunRaysCompositePS, main.RTV, sunRaysTexB->srv.get(), viewport, additiveBlendState.get(), "Effects11::SunRays Composite");

	stateBackup.Restore(context);
	stateBackup.Release();
}
