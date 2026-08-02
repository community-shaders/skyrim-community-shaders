#include "Effects11.h"

#include <DirectXTex.h>

#include "Effects11/D3D11StateBackup.h"
#include "Effects11/ENBHelper.h"
#include "Effects11/EffectManager.h"
#include "Effects11/MenuManager.h"
#include "Effects11/PresetManager.h"
#include "Effects11/SettingManager.h"
#include "Effects11/WeatherManager.h"

#include "CloudShadows.h"
#include "Deferred.h"
#include "IBL.h"
#include "ShaderCache.h"
#include "State.h"
#include "TerrainShadows.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

Effects11::PerFrame Effects11::GetCommonBufferData()
{
	if (!loaded)
		return {};

	CheckCommonData();

	auto& settingManager = SettingManager::GetSingleton();
	PerFrame data{};

	data.Enable = enableEffect;
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

	data.EnableVolumetricRays = enableEffect && settingManager.GetValue<bool>("EnableVolumetricRays", "EFFECT");
	data.VolumetricRaysIntensity = settingManager.GetInterpolatedTimeOfDayValue("Intensity", "VOLUMETRICRAYS");
	{
		float density = std::max(0.1f, settingManager.GetInterpolatedTimeOfDayValue("Density", "VOLUMETRICRAYS"));
		data.VolumetricRaysExtinction = 0.000003f / density;
	}
	data.VolumetricRaysSkyColorAmount = settingManager.GetInterpolatedTimeOfDayValue("SkyColorAmount", "VOLUMETRICRAYS");

	data.EnableRain = enableEffect && raindropSRV;
	data.RainMotionStretch = settingManager.GetInterpolatedTimeOfDayValue("MotionStretch", "RAIN");
	data.RainMotionTransparency = settingManager.GetInterpolatedTimeOfDayValue("MotionTransparency", "RAIN");

	data.FireIntensity = settingManager.GetInterpolatedTimeOfDayValue("FireIntensity", "FIRE");
	data.FireCurve = settingManager.GetInterpolatedTimeOfDayValue("FireCurve", "FIRE");

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

void Effects11::DrawSettings()
{
	MenuManager::GetSingleton().RenderImGui();
}

void Effects11::LoadRaindropTexture()
{
	raindropTexture = nullptr;
	raindropSRV = nullptr;
	raindropStatus.clear();

	auto& presetManager = PresetManager::GetSingleton();
	auto enbPath = presetManager.GetENBSeriesPath();
	auto raindropPath = enbPath / "enbraindrops.png";

	if (!std::filesystem::exists(raindropPath)) {
		raindropStatus = "Texture not found: enbraindrops.png";
		logger::debug("[Effects11] Raindrop texture not found: {}", raindropPath.string());
		return;
	}

	std::wstring widePath = raindropPath.wstring();

	DirectX::ScratchImage image;
	HRESULT hr = DirectX::LoadFromWICFile(widePath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, image);
	if (FAILED(hr)) {
		raindropStatus = std::format("Failed to load texture (invalid image, HRESULT 0x{:08X})", static_cast<uint32_t>(hr));
		logger::error("[Effects11] Failed to load raindrop texture: {}", raindropPath.string());
		return;
	}

	DirectX::ScratchImage mipImage;
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
		DirectX::TEX_FILTER_DEFAULT, 0, mipImage);
	if (FAILED(hr)) {
		raindropStatus = std::format("Failed to generate mipmaps (HRESULT 0x{:08X})", static_cast<uint32_t>(hr));
		logger::error("[Effects11] Failed to generate mipmaps for raindrop texture");
		return;
	}

	DirectX::ScratchImage bc7Image;
	hr = DirectX::Compress(mipImage.GetImages(), mipImage.GetImageCount(), mipImage.GetMetadata(),
		DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_BC7_QUICK, 1.0f, bc7Image);
	if (FAILED(hr)) {
		raindropStatus = std::format("Failed to compress texture (HRESULT 0x{:08X})", static_cast<uint32_t>(hr));
		logger::error("[Effects11] Failed to compress raindrop texture to BC7");
		return;
	}

	auto device = globals::d3d::device;
	hr = DirectX::CreateTexture(device,
		bc7Image.GetImages(), bc7Image.GetImageCount(), bc7Image.GetMetadata(),
		reinterpret_cast<ID3D11Resource**>(raindropTexture.put()));
	if (FAILED(hr)) {
		raindropStatus = std::format("Failed to create GPU texture (HRESULT 0x{:08X})", static_cast<uint32_t>(hr));
		logger::error("[Effects11] Failed to create raindrop GPU texture");
		return;
	}

	Util::SetResourceName(raindropTexture.get(), "Effects11::RaindropTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_BC7_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(bc7Image.GetMetadata().mipLevels);
	srvDesc.Texture2D.MostDetailedMip = 0;

	hr = device->CreateShaderResourceView(raindropTexture.get(), &srvDesc, raindropSRV.put());
	if (FAILED(hr)) {
		raindropStatus = std::format("Failed to create shader resource view (HRESULT 0x{:08X})", static_cast<uint32_t>(hr));
		logger::error("[Effects11] Failed to create raindrop SRV");
		raindropTexture = nullptr;
		return;
	}

	Util::SetResourceName(raindropSRV.get(), "Effects11::RaindropTexture SRV");

	logger::info("[Effects11] Loaded raindrop texture: {} ({}x{}, BC7, {} mips)",
		raindropPath.string(),
		bc7Image.GetMetadata().width,
		bc7Image.GetMetadata().height,
		bc7Image.GetMetadata().mipLevels);
}

void Effects11::SetupResources()
{
	EffectManager::GetSingleton().Initialize();
	LoadRaindropTexture();
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

	EffectManager::GetSingleton().ReloadShaders();
}

void Effects11::Prepass()
{
	if (!enableEffect) {
		return;
	}

	auto& settingManager = SettingManager::GetSingleton();

	auto imageSpaceManager = globals::game::imageSpaceManager;
	if (!imageSpaceManager) {
		return;
	}

	auto& data = imageSpaceManager->GetRuntimeData().data;

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

		float sunlightScale = FLT_MIN;
		auto imageSpaceManager = globals::game::imageSpaceManager;
		if (imageSpaceManager) {
			sunlightScale = std::max(imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale, FLT_MIN);
		}
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

	if (enableEffect) {
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

void Effects11::CheckCommonData()
{
	static Util::FrameChecker checker;
	if (checker.IsNewFrame()) {
		ENBHelper::Update();

		auto& settingManager = SettingManager::GetSingleton();
		auto ui = globals::game::ui;
		bool isMenuOpen = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
		enableEffect = !isMenuOpen && globals::shaderCache->IsEnabled() && settingManager.GetValue<bool>("UseEffect", "GLOBAL");

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

void Effects11::OverridePointLightColor(float3& a_color)
{
	auto& settingManager = SettingManager::GetSingleton();

	a_color = Curve(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingCurve", "ENVIRONMENT"));
	a_color = Desaturation(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingDesaturation", "ENVIRONMENT"));
	a_color = Intensity(a_color, settingManager.GetInterpolatedTimeOfDayValue("PointLightingIntensity", "ENVIRONMENT"));
}

void Effects11::OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors)
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

void Effects11::OnSkyUpdateColors(RE::Sky* a_sky)
{
	CheckCommonData();
	if (enableEffect)
		OverrideWeather(a_sky);
}

bool Effects11::HandleTonemapRender(RE::RENDER_TARGET a_input, RE::RENDER_TARGET a_output)
{
	CheckCommonData();

	auto& settingManager = SettingManager::GetSingleton();
	auto& effectManager = EffectManager::GetSingleton();

	if (enableEffect && !settingManager.GetValue<bool>("UseOriginalPostProcessing", "EFFECT")) {
		auto& renderTargets = globals::game::renderer->GetRuntimeData().renderTargets;
		effectManager.ExecuteEffects(renderTargets[a_input], renderTargets[a_output]);
		return true;
	}
	return false;
}

void Effects11::ModifySky(RE::BSRenderPass* Pass)
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


void Effects11::ModifyParticle(RE::BSRenderPass* Pass)
{
	if (!enableEffect || !raindropSRV)
		return;

	if (!Pass)
		return;

	auto state = globals::state;
	if (state->currentPixelDescriptor != static_cast<uint32_t>(SIE::ShaderCache::ParticleShaderTechniques::EnvCubeRain))
		return;

	auto context = globals::d3d::context;
	ID3D11ShaderResourceView* srv = raindropSRV.get();
	context->PSSetShaderResources(80, 1, &srv);

	ID3D11Buffer* cbs[] = { globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	context->VSSetConstantBuffers(5, 2, cbs);
}


void Effects11::ParticleShaderHacks()
{
	if (!enableEffect || !raindropSRV)
		return;

	auto state = globals::state;
	if (!state->currentShader || state->currentShader->shaderType.get() != RE::BSShader::Type::Particle)
		return;
	if (state->currentPixelDescriptor != static_cast<uint32_t>(SIE::ShaderCache::ParticleShaderTechniques::EnvCubeRain))
		return;

	auto context = globals::d3d::context;

	if (!alphaBlendState) {
		D3D11_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		globals::d3d::device->CreateBlendState(&blendDesc, alphaBlendState.put());
	}

	float blendFactor[4] = { 0, 0, 0, 0 };
	context->OMSetBlendState(alphaBlendState.get(), blendFactor, 0xFFFFFFFF);
}

void Effects11::DrawVolumetricRays()
{
	if (!enableEffect)
		return;

	if (Util::IsInterior())
		return;

	if (globals::game::sky && globals::game::sky->flags.any(RE::Sky::Flags::kHideSky))
		return;

	if (globals::state->isMapMenuOpen)
		return;

	auto& settingManager = SettingManager::GetSingleton();
	if (!settingManager.GetValue<bool>("EnableVolumetricRays", "EFFECT"))
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
		std::vector<std::pair<const char*, const char*>> defines;
		if (globals::features::ibl.loaded)
			defines.push_back({ "IBL", nullptr });

		applyVolumetricRaysPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\ApplyVolumetricRaysPS.hlsl", defines, "ps_5_0"));
		if (!applyVolumetricRaysPS)
			return;
	}

	if (!blurHCS) {
		blurHCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ISVolumetricLightingBlurHCS.hlsl", {}, "cs_5_0"));
		if (!blurHCS)
			return;
	}

	if (!blurVCS) {
		blurVCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ISVolumetricLightingBlurVCS.hlsl", {}, "cs_5_0"));
		if (!blurVCS)
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
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
		globals::d3d::device->CreateBlendState(&blendDesc, additiveBlendState.put());
	}

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
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = halfTexWidth;
		desc.Height = halfTexHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		vlTexA = std::make_unique<Texture2D>(desc, "Effects11::VLTexA");
		vlTexA->CreateSRV(srvDesc);
		vlTexA->CreateRTV(rtvDesc);
		vlTexA->CreateUAV(uavDesc);

		vlTexB = std::make_unique<Texture2D>(desc, "Effects11::VLTexB");
		vlTexB->CreateSRV(srvDesc);
		vlTexB->CreateUAV(uavDesc);

		D3D11_TEXTURE2D_DESC depthDesc = desc;
		depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
		depthDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = srvDesc;
		depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;

		D3D11_RENDER_TARGET_VIEW_DESC depthRtvDesc = rtvDesc;
		depthRtvDesc.Format = DXGI_FORMAT_R32_FLOAT;

		vlDepthHalf = std::make_unique<Texture2D>(depthDesc, "Effects11::VLDepthHalf");
		vlDepthHalf->CreateSRV(depthSrvDesc);
		vlDepthHalf->CreateRTV(depthRtvDesc);
	}

	if (!vlBlurCB)
		vlBlurCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc(16), "Effects11::VLBlurCB");

	Effects11Util::D3D11ScopedPostFxBackup stateBackup;
	stateBackup.Save(context);

	ID3D11SamplerState* sampler = Deferred::GetSingleton()->linearSampler;
	D3D11_VIEWPORT viewport{ 0, 0, resolution.x, resolution.y, 0, 1 };
	D3D11_VIEWPORT halfViewport{ 0, 0, static_cast<float>(halfDynWidth), static_cast<float>(halfDynHeight), 0, 1 };

	auto* profiler = globals::profiler;

	// Pass 1: Raymarch shadow + depth → half-res textures (MRT)
	{
		profiler->BeginPass("Effects11::VolumetricRays Pass 0");

		ID3D11RenderTargetView* rtvs[2] = { vlTexA->rtv.get(), vlDepthHalf->rtv.get() };
		context->OMSetRenderTargets(2, rtvs, nullptr);
		context->RSSetViewports(1, &halfViewport);

		context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		context->RSSetState(effectManager.rasterizerState.get());
		context->OMSetDepthStencilState(nullptr, 0);

		UINT stride = 20;
		UINT offset = 0;
		ID3D11Buffer* vbs[] = { effectManager.quadVertexBuffer.get() };
		context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
		context->IASetInputLayout(effectManager.inputLayout.get());
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		context->VSSetShader(effectManager.copyVertexShader.get(), nullptr, 0);
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
		context->RSSetState(effectManager.rasterizerState.get());
		context->OMSetDepthStencilState(nullptr, 0);

		UINT stride = 20;
		UINT offset = 0;
		ID3D11Buffer* vbs[] = { effectManager.quadVertexBuffer.get() };
		context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
		context->IASetInputLayout(effectManager.inputLayout.get());
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		context->VSSetShader(effectManager.copyVertexShader.get(), nullptr, 0);
		context->PSSetShader(applyVolumetricRaysPS, nullptr, 0);

		auto& ibl = globals::features::ibl;
		ID3D11ShaderResourceView* srvs[16]{};
		srvs[0] = vlTexA->srv.get();
		srvs[1] = vlDepthHalf->srv.get();
		if (ibl.loaded) {
			srvs[14] = ibl.envIBLTexture->srv.get();
			srvs[15] = ibl.skyIBLTexture->srv.get();
		}
		context->PSSetShaderResources(0, 16, srvs);
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
