#include "ExponentialHeightFog.h"

#include "Deferred.h"
#include "Effects11.h"
#include "Effects11/SettingManager.h"
#include "Features/CloudShadows.h"
#include "Features/IBL.h"
#include "Features/LightLimitFix.h"
#include "Features/Skylighting.h"
#include "Features/TerrainShadows.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "WeatherVariableRegistry.h"

#define I18N_KEY_PREFIX "feature.exp_height_fog."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExponentialHeightFog::Settings,
	enabled,
	useDynamicCubemaps,
	startDistance,
	fogHeight,
	fogHeightFalloff,
	fogDensity,
	fogHeight2,
	fogHeightFalloff2,
	fogDensity2,
	directionalInscatteringMultiplier,
	directionalInscatteringAnisotropy,
	useSkyIBL,
	inscatteringTint,
	cubemapMipLevel,
	sunlightAttenuationAmount,
	respectVanillaFogFade,
	disableVanillaFog,
	fogInscatteringColor,
	originalFogColorAmount,
	volumetricFogEnabled,
	volumetricGridPixelSize,
	volumetricGridSizeZ,
	volumetricFogDistance,
	volumetricFogStartDistance,
	volumetricFogNearFadeInDistance,
	volumetricFogExtinctionScale,
	volumetricFogScatteringDistribution,
	volumetricFogAlbedo,
	volumetricFogEmissive,
	volumetricDirectionalScatteringIntensity,
	volumetricShadowBias,
	volumetricDepthDistributionScale,
	volumetricSkyLightingIntensity,
	volumetricHistoryWeight,
	volumetricHistoryMissSampleCount,
	volumetricSampleJitterMultiplier,
	volumetricUpsampleJitterMultiplier,
	volumetricNearGridDistance,
	volumetricFarGridPixelSize,
	volumetricFarGridSizeZ,
	volumetricFogNoiseScale,
	volumetricFogNoiseThreshold,
	volumetricFogNoiseVelocity,
	volumetricLocalLightScatteringIntensity)

namespace
{
	float Halton(uint32_t a_index, uint32_t a_base)
	{
		float result = 0.0f;
		float invBase = 1.0f / static_cast<float>(a_base);
		float fraction = invBase;
		while (a_index > 0) {
			result += static_cast<float>(a_index % a_base) * fraction;
			a_index /= a_base;
			fraction *= invBase;
		}
		return result;
	}
}

void ExponentialHeightFog::RestoreDefaultSettings()
{
	settings = {};
}

void ExponentialHeightFog::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ExponentialHeightFog::SaveSettings(json& o_json)
{
	o_json = settings;
}

ExponentialHeightFog::Settings ExponentialHeightFog::GetCommonBufferData() const
{
	Settings data = settings;
	if (IsSuppressed())
		data.enabled = 0;
	return data;
}

bool ExponentialHeightFog::IsSuppressed() const
{
	// The flat world map keeps its vanilla fog; the 3D map runs on the scene kept from before it opened
	return (globals::features::effects11.loaded && globals::features::effects11.enableEffect) || globals::state->IsFlatWorldMapOpen();
}

void ExponentialHeightFog::DrawSettings()
{
	if (globals::features::effects11.loaded) {
		auto& enb = globals::features::effects11;
		if (enb.enableEffect) {
			ImGui::TextColored(globals::menu->GetSettings().Theme.StatusPalette.Warning, "%s", T("common.settings_managed_by_enb", "Settings are currently managed by ENB."));
			return;
		}
	}

	ImGui::Checkbox(T(TKEY("enable_exp_height_fog"), "Enable Exponential Height Fog"), (bool*)&settings.enabled);
	Util::WeatherUI::SliderFloat(T(TKEY("start_distance"), "Start Distance"), this, "startDistance", &settings.startDistance, 0.0f, 100000.0f, "%.1f");
	Util::WeatherUI::SliderFloat(T(TKEY("fog_height"), "Fog Height"), this, "fogHeight", &settings.fogHeight, -22000.0f, 22000.0f, "%.1f");
	Util::WeatherUI::SliderFloat(T(TKEY("fog_height_falloff"), "Fog Height Falloff"), this, "fogHeightFalloff", &settings.fogHeightFalloff, 0.001f, 2.0f, "%.3f");
	Util::WeatherUI::ColorEdit4(T(TKEY("fog_inscattering_color"), "Fog Inscattering Color"), this, "fogInscatteringColor", (float*)&settings.fogInscatteringColor);
	Util::WeatherUI::SliderFloat(T(TKEY("original_fog_color_amount"), "Original Fog Color Amount"), this, "originalFogColorAmount", &settings.originalFogColorAmount, 0.0f, 1.0f, "%.2f");
	Util::WeatherUI::SliderFloat(T(TKEY("fog_density"), "Fog Density"), this, "fogDensity", &settings.fogDensity, 0.0f, 1.0f, "%.3f");
	if (ImGui::TreeNode(T(TKEY("second_fog_layer"), "Second Fog Layer"))) {
		Util::WeatherUI::SliderFloat(T(TKEY("fog_height_2"), "Fog Height 2"), this, "fogHeight2", &settings.fogHeight2, -22000.0f, 22000.0f, "%.1f");
		Util::WeatherUI::SliderFloat(T(TKEY("fog_height_falloff_2"), "Fog Height Falloff 2"), this, "fogHeightFalloff2", &settings.fogHeightFalloff2, 0.001f, 2.0f, "%.3f");
		Util::WeatherUI::SliderFloat(T(TKEY("fog_density_2"), "Fog Density 2"), this, "fogDensity2", &settings.fogDensity2, 0.0f, 1.0f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("second_fog_layer_tooltip"),
								  "Adds a second stacked exponential height fog layer with its own base height, density and height falloff.\n"
								  "The two line integrals are summed.\n"
								  "Use it for high-altitude haze above the ground layer or a distinct low-lying ground fog."));
		}
		ImGui::TreePop();
	}
	Util::WeatherUI::SliderFloat(T(TKEY("dir_inscattering_mul"), "Directional Light Inscattering Multiplier"), this, "directionalInscatteringMultiplier", &settings.directionalInscatteringMultiplier, 0.0f, 10.0f, "%.2f");
	Util::WeatherUI::SliderFloat(T(TKEY("sunlight_attenuation"), "Sunlight Attenuation Amount"), this, "sunlightAttenuationAmount", &settings.sunlightAttenuationAmount, 0.0f, 1.0f, "%.2f");
	Util::WeatherUI::SliderFloat(T(TKEY("dir_inscattering_anisotropy"), "Directional Light Inscattering Anisotropy"), this, "directionalInscatteringAnisotropy", &settings.directionalInscatteringAnisotropy, -0.99f, 0.99f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("dir_inscattering_anisotropy_tooltip"),
							  "Controls the asymmetry of inscattering via the Henyey-Greenstein phase function.\n"
							  "Positive values produce forward scattering (glow around sun).\n"
							  "Zero is isotropic. Negative values produce back scattering."));
	}
	ImGui::Checkbox(T(TKEY("disable_vanilla_fog"), "Disable Vanilla Fog"), (bool*)&settings.disableVanillaFog);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_vanilla_fog_tooltip"), "Disables the vanilla fog entirely. Only exponential height fog will be applied."));
	}
	Util::WeatherUI::Checkbox(T(TKEY("apply_vanilla_fade"), "Apply Vanilla Fade"), this, "respectVanillaFogFade", (bool*)&settings.respectVanillaFogFade);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("apply_vanilla_fade_tooltip"), "Applies vanilla fade brightness to exponential height fog."));
	}
	ImGui::Checkbox(T(TKEY("use_sky_ibl"), "Use Sky IBL for Exterior Inscattering"), (bool*)&settings.useSkyIBL);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("use_sky_ibl_tooltip"), "Adds the sky IBL color to the fog inscattering in exteriors. Requires the Image Based Lighting feature."));
	}
	ImGui::Checkbox(T(TKEY("use_dynamic_cubemaps"), "Use Dynamic Cubemaps for Interior Inscattering"), (bool*)&settings.useDynamicCubemaps);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("use_dynamic_cubemaps_tooltip"), "Adds the dynamic environment cubemap color to the fog inscattering in interiors."));
	}
	Util::WeatherUI::ColorEdit4(T(TKEY("inscattering_cubemap_tint"), "Inscattering Cubemap Tint"), this, "inscatteringTint", (float*)&settings.inscatteringTint);
	ImGui::SliderFloat(T(TKEY("cubemap_mip_level"), "Cubemap Mip Level"), &settings.cubemapMipLevel, 1.0f, 8.0f, "%.1f");

	ImGui::SeparatorText(T(TKEY("volumetric_fog"), "Volumetric Fog"));
	Util::WeatherUI::Checkbox(T(TKEY("enable_volumetric_fog"), "Enable Volumetric Fog"), this, "volumetricFogEnabled", (bool*)&settings.volumetricFogEnabled);
	if (settings.volumetricFogEnabled) {
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_view_distance"), "Volumetric View Distance"), this, "volumetricFogDistance", &settings.volumetricFogDistance, 1000.0f, 200000.0f, "%.0f");
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_start_distance"), "Volumetric Start Distance"), this, "volumetricFogStartDistance", &settings.volumetricFogStartDistance, 0.0f, 20000.0f, "%.0f");
		Util::WeatherUI::SliderFloat(T(TKEY("near_fade_in_distance"), "Near Fade In Distance"), this, "volumetricFogNearFadeInDistance", &settings.volumetricFogNearFadeInDistance, 0.0f, 20000.0f, "%.0f");
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_near_grid_distance"), "Near Grid Distance"), this, "volumetricNearGridDistance", &settings.volumetricNearGridDistance, 256.0f, 50000.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("volumetric_near_grid_distance_tooltip"),
								  "Distance covered by the full-resolution near volume.\n"
								  "A second, quarter-lattice far volume covers the remaining distance up to the Volumetric View Distance.\n"
								  "Smaller values improve near-field resolution; larger values move the low-resolution far volume farther away."));
		}
		if (ImGui::TreeNode(T(TKEY("volumetric_noise"), "Volumetric Noise"))) {
			Util::WeatherUI::SliderFloat(T(TKEY("volumetric_noise_scale"), "Noise Scale"), this, "volumetricFogNoiseScale", &settings.volumetricFogNoiseScale, 0.0f, 0.01f, "%.6f");
			Util::WeatherUI::SliderFloat(T(TKEY("volumetric_noise_threshold"), "Noise Threshold"), this, "volumetricFogNoiseThreshold", &settings.volumetricFogNoiseThreshold, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat3(T(TKEY("volumetric_noise_velocity"), "Noise Velocity"), &settings.volumetricFogNoiseVelocity.x, -1.0f, 1.0f, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("volumetric_noise_tooltip"),
									  "Modulates the volumetric fog density with a 3D value noise field.\n"
									  "Noise Scale: spatial frequency of the fog clumps (0 = disabled).\n"
									  "Noise Threshold: soft cutoff that carves clumps out of the noise.\n"
									  "Noise Velocity: animation drift of the noise field, scaled by time."));
			}
			ImGui::TreePop();
		}
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_extinction_scale"), "Volumetric Extinction Scale"), this, "volumetricFogExtinctionScale", &settings.volumetricFogExtinctionScale, 0.0f, 10.0f, "%.2f");
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_scattering_distribution"), "Volumetric Scattering Distribution"), this, "volumetricFogScatteringDistribution", &settings.volumetricFogScatteringDistribution, -0.9f, 0.9f, "%.2f");
		Util::WeatherUI::ColorEdit4(T(TKEY("volumetric_albedo"), "Volumetric Albedo"), this, "volumetricFogAlbedo", (float*)&settings.volumetricFogAlbedo);
		Util::WeatherUI::ColorEdit4(T(TKEY("volumetric_emissive"), "Volumetric Emissive"), this, "volumetricFogEmissive", (float*)&settings.volumetricFogEmissive);
		Util::WeatherUI::SliderFloat(T(TKEY("directional_scattering_intensity"), "Directional Scattering Intensity"), this, "volumetricDirectionalScatteringIntensity", &settings.volumetricDirectionalScatteringIntensity, 0.0f, 10.0f, "%.2f");
		Util::WeatherUI::SliderFloat(T(TKEY("sky_lighting_scattering_intensity"), "Sky Lighting Scattering Intensity"), this, "volumetricSkyLightingIntensity", &settings.volumetricSkyLightingIntensity, 0.0f, 10.0f, "%.2f");
		Util::WeatherUI::SliderFloat(T(TKEY("local_light_scattering_intensity"), "Local Light Scattering Intensity"), this, "volumetricLocalLightScatteringIntensity", &settings.volumetricLocalLightScatteringIntensity, 0.0f, 10.0f, "%.2f");
		if (ImGui::TreeNode(T(TKEY("debug"), "Debug"))) {
			uint32_t minGridPixelSize = 4;
			uint32_t maxGridPixelSize = 64;
			uint32_t minGridSizeZ = 16;
			uint32_t maxGridSizeZ = 160;
			ImGui::SliderScalar(T(TKEY("grid_pixel_size"), "Grid Pixel Size"), ImGuiDataType_U32, &settings.volumetricGridPixelSize, &minGridPixelSize, &maxGridPixelSize, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderScalar(T(TKEY("grid_depth_slices"), "Grid Depth Slices"), ImGuiDataType_U32, &settings.volumetricGridSizeZ, &minGridSizeZ, &maxGridSizeZ, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderScalar(T(TKEY("far_grid_pixel_size"), "Far Grid Pixel Size"), ImGuiDataType_U32, &settings.volumetricFarGridPixelSize, &minGridPixelSize, &maxGridPixelSize, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderScalar(T(TKEY("far_grid_depth_slices"), "Far Grid Depth Slices"), ImGuiDataType_U32, &settings.volumetricFarGridSizeZ, &minGridSizeZ, &maxGridSizeZ, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("directional_shadow_bias"), "Directional Shadow Bias"), &settings.volumetricShadowBias, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("depth_distribution_scale"), "Depth Distribution Scale"), &settings.volumetricDepthDistributionScale, 1.0f, 128.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("temporal_history_weight"), "Temporal History Weight"), &settings.volumetricHistoryWeight, 0.0f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			uint32_t minHistoryMissSampleCount = 1;
			uint32_t maxHistoryMissSampleCount = 16;
			ImGui::SliderScalar(T(TKEY("history_miss_samples"), "History Miss Samples"), ImGuiDataType_U32, &settings.volumetricHistoryMissSampleCount, &minHistoryMissSampleCount, &maxHistoryMissSampleCount, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("sample_jitter_multiplier"), "Sample Jitter Multiplier"), &settings.volumetricSampleJitterMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("sample_jitter_multiplier_tooltip"),
									  "Adds per-voxel random offset on top of the Halton sequence."));
			}
			ImGui::SliderFloat(T(TKEY("upsample_jitter_multiplier"), "Upsample Jitter Multiplier"), &settings.volumetricUpsampleJitterMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("upsample_jitter_multiplier_tooltip"),
									  "Jitters the final 3D fog lookup in screen space to hide\n"
									  "low-resolution froxel pixelization."));
			}
			ImGui::TreePop();
		}
	}
}

void ExponentialHeightFog::SetupResources()
{
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&samplerDesc, linearSampler.put()));
	Util::SetResourceName(linearSampler.get(), "ExponentialHeightFog::LinearSampler");

	samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&samplerDesc, shadowSampler.put()));
	Util::SetResourceName(shadowSampler.get(), "ExponentialHeightFog::ShadowSampler");

	volumetricFogCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<VolumetricFogCB>(), "ExponentialHeightFog::VolumetricFogCB");
}

void ExponentialHeightFog::ClearShaderCache()
{
	if (materialSetupCS) {
		materialSetupCS->Release();
		materialSetupCS = nullptr;
	}
	if (farMaterialSetupCS) {
		farMaterialSetupCS->Release();
		farMaterialSetupCS = nullptr;
	}
	if (conservativeDepthCS) {
		conservativeDepthCS->Release();
		conservativeDepthCS = nullptr;
	}
	if (farConservativeDepthCS) {
		farConservativeDepthCS->Release();
		farConservativeDepthCS = nullptr;
	}
	if (lightScatteringCS) {
		lightScatteringCS->Release();
		lightScatteringCS = nullptr;
	}
	if (farLightScatteringCS) {
		farLightScatteringCS->Release();
		farLightScatteringCS = nullptr;
	}
	if (integrationCS) {
		integrationCS->Release();
		integrationCS = nullptr;
	}
	if (farIntegrationCS) {
		farIntegrationCS->Release();
		farIntegrationCS = nullptr;
	}
}

void ExponentialHeightFog::CaptureDirectionalShadowMap()
{
	ID3D11ShaderResourceView* shadowMap = nullptr;
	globals::d3d::context->PSGetShaderResources(4, 1, &shadowMap);
	directionalShadowMap.copy_from(shadowMap);
	if (shadowMap)
		shadowMap->Release();
}

void ExponentialHeightFog::EnsureVolumetricResources()
{
	uint32_t pixelSize = std::clamp(settings.volumetricGridPixelSize, 4u, 64u);
	const uint32_t gridZ = std::clamp(settings.volumetricGridSizeZ, 16u, 160u);
	uint32_t farPixelSize = std::clamp(settings.volumetricFarGridPixelSize, 4u, 64u);
	const uint32_t farGridZ = std::clamp(settings.volumetricFarGridSizeZ, 16u, 160u);
	float2 screenSz{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSz);

	auto getGridSize = [&renderSize](uint32_t a_pixelSize, uint32_t a_gridZ) {
		return DirectX::XMUINT4{
			std::max(1u, static_cast<uint32_t>(std::ceil(renderSize.x / static_cast<float>(a_pixelSize)))),
			std::max(1u, static_cast<uint32_t>(std::ceil(renderSize.y / static_cast<float>(a_pixelSize)))),
			a_gridZ,
			0u
		};
	};
	DirectX::XMUINT4 gridSize = getGridSize(pixelSize, gridZ);

	constexpr uint64_t maxVolumeVoxels = 16ull * 1024ull * 1024ull;
	while (pixelSize < 64u &&
		   static_cast<uint64_t>(gridSize.x) * gridSize.y * gridSize.z > maxVolumeVoxels) {
		pixelSize++;
		gridSize = getGridSize(pixelSize, gridZ);
	}

	// The far volume must be coarser than the near volume.
	farPixelSize = std::max(farPixelSize, pixelSize);
	DirectX::XMUINT4 farGridSize = getGridSize(farPixelSize, farGridZ);
	while (farPixelSize < 64u &&
		   static_cast<uint64_t>(farGridSize.x) * farGridSize.y * farGridSize.z > maxVolumeVoxels / 4ull) {
		farPixelSize++;
		farGridSize = getGridSize(farPixelSize, farGridZ);
	}

	if (vBufferA &&
		currentGridSize.x == gridSize.x && currentGridSize.y == gridSize.y && currentGridSize.z == gridSize.z &&
		currentFarGridSize.x == farGridSize.x && currentFarGridSize.y == farGridSize.y && currentFarGridSize.z == farGridSize.z)
		return;

	currentGridSize = gridSize;
	currentFarGridSize = farGridSize;

	auto make3D = [this](const DirectX::XMUINT4& a_size, const char* a_name) {
		D3D11_TEXTURE3D_DESC texDesc{};
		texDesc.Width = a_size.x;
		texDesc.Height = a_size.y;
		texDesc.Depth = a_size.z;
		texDesc.MipLevels = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D.MipLevels = 1;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		uavDesc.Texture3D.MipSlice = 0;
		uavDesc.Texture3D.FirstWSlice = 0;
		uavDesc.Texture3D.WSize = a_size.z;

		auto tex = std::make_unique<Texture3D>(texDesc, a_name);
		tex->CreateSRV(srvDesc);
		tex->CreateUAV(uavDesc);
		return tex;
	};

	auto make2D = [this](const DirectX::XMUINT4& a_size, const char* a_name) {
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = a_size.x;
		texDesc.Height = a_size.y;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		auto tex = std::make_unique<Texture2D>(texDesc, a_name);
		tex->CreateSRV(srvDesc);
		tex->CreateUAV(uavDesc);
		return tex;
	};

	vBufferA = make3D(gridSize, "ExponentialHeightFog::VBufferA");
	conservativeDepth = make2D(gridSize, "ExponentialHeightFog::ConservativeDepth");
	lightScattering = make3D(gridSize, "ExponentialHeightFog::LightScattering");
	integratedLightScattering = make3D(gridSize, "ExponentialHeightFog::IntegratedLightScattering");

	conservativeDepthHistory = std::make_unique<Texture2D>(conservativeDepth->desc, "ExponentialHeightFog::ConservativeDepthHistory");
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = conservativeDepth->desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		conservativeDepthHistory->CreateSRV(srvDesc);
	}

	lightScatteringHistory = std::make_unique<Texture3D>(lightScattering->desc, "ExponentialHeightFog::LightScatteringHistory");
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = lightScattering->desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D.MipLevels = 1;
		lightScatteringHistory->CreateSRV(srvDesc);
	}

	vBufferAFar = make3D(farGridSize, "ExponentialHeightFog::VBufferAFar");
	conservativeDepthFar = make2D(farGridSize, "ExponentialHeightFog::ConservativeDepthFar");
	lightScatteringFar = make3D(farGridSize, "ExponentialHeightFog::LightScatteringFar");
	integratedLightScatteringFar = make3D(farGridSize, "ExponentialHeightFog::IntegratedLightScatteringFar");

	conservativeDepthFarHistory = std::make_unique<Texture2D>(conservativeDepthFar->desc, "ExponentialHeightFog::ConservativeDepthFarHistory");
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = conservativeDepthFar->desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		conservativeDepthFarHistory->CreateSRV(srvDesc);
	}

	lightScatteringFarHistory = std::make_unique<Texture3D>(lightScatteringFar->desc, "ExponentialHeightFog::LightScatteringFarHistory");
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = lightScatteringFar->desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D.MipLevels = 1;
		lightScatteringFarHistory->CreateSRV(srvDesc);
	}

	hasLightScatteringHistory = false;
	hasConservativeDepthHistory = false;
	hasLightScatteringFarHistory = false;
	hasConservativeDepthFarHistory = false;
	lastPrepassFrame = UINT32_MAX;
}

void ExponentialHeightFog::ReleaseVolumetricResources()
{
	vBufferA.reset();
	vBufferAFar.reset();
	conservativeDepth.reset();
	conservativeDepthHistory.reset();
	conservativeDepthFar.reset();
	conservativeDepthFarHistory.reset();
	lightScattering.reset();
	lightScatteringHistory.reset();
	lightScatteringFar.reset();
	lightScatteringFarHistory.reset();
	integratedLightScattering.reset();
	integratedLightScatteringFar.reset();
	currentGridSize = {};
	currentFarGridSize = {};
	hasLightScatteringHistory = false;
	hasConservativeDepthHistory = false;
	hasLightScatteringFarHistory = false;
	hasConservativeDepthFarHistory = false;
	lastPrepassFrame = UINT32_MAX;
	ID3D11ShaderResourceView* nullSRV = nullptr;
	globals::d3d::context->PSSetShaderResources(19, 1, &nullSRV);
	globals::d3d::context->PSSetShaderResources(22, 1, &nullSRV);
}

void ExponentialHeightFog::BindIntegratedLightScattering()
{
	ID3D11ShaderResourceView* srv = integratedLightScattering ? integratedLightScattering->srv.get() : nullptr;
	globals::d3d::context->PSSetShaderResources(19, 1, &srv);
	ID3D11ShaderResourceView* farSrv = integratedLightScatteringFar ? integratedLightScatteringFar->srv.get() : nullptr;
	globals::d3d::context->PSSetShaderResources(22, 1, &farSrv);
}

ID3D11ComputeShader* ExponentialHeightFog::GetMaterialSetupCS()
{
	if (!materialSetupCS)
		materialSetupCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogMaterialCS.hlsl", {}, "cs_5_0"));
	return materialSetupCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetConservativeDepthCS()
{
	if (!conservativeDepthCS)
		conservativeDepthCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogConservativeDepthCS.hlsl", {}, "cs_5_0"));
	return conservativeDepthCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetLightScatteringCS()
{
	if (!lightScatteringCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		if (globals::features::lightLimitFix.loaded) {
			defines.emplace_back("LIGHT_LIMIT_FIX", "");
		}
		if (globals::features::terrainShadows.loaded) {
			defines.emplace_back("TERRAIN_SHADOWS", "");
		}
		if (globals::features::cloudShadows.loaded) {
			defines.emplace_back("CLOUD_SHADOWS", "");
		}
		lightScatteringCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogLightScatteringCS.hlsl", defines, "cs_5_0"));
	}
	return lightScatteringCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetFarLightScatteringCS()
{
	if (!farLightScatteringCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		defines.emplace_back("VOLUMETRIC_FOG_FAR_GRID", "");
		if (globals::features::lightLimitFix.loaded) {
			defines.emplace_back("LIGHT_LIMIT_FIX", "");
		}
		if (globals::features::terrainShadows.loaded) {
			defines.emplace_back("TERRAIN_SHADOWS", "");
		}
		if (globals::features::cloudShadows.loaded) {
			defines.emplace_back("CLOUD_SHADOWS", "");
		}
		farLightScatteringCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogLightScatteringCS.hlsl", defines, "cs_5_0"));
	}
	return farLightScatteringCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetFarMaterialSetupCS()
{
	if (!farMaterialSetupCS)
		farMaterialSetupCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogMaterialCS.hlsl", { { "VOLUMETRIC_FOG_FAR_GRID", "" } }, "cs_5_0"));
	return farMaterialSetupCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetFarConservativeDepthCS()
{
	if (!farConservativeDepthCS)
		farConservativeDepthCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogConservativeDepthCS.hlsl", { { "VOLUMETRIC_FOG_FAR_GRID", "" } }, "cs_5_0"));
	return farConservativeDepthCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetIntegrationCS()
{
	if (!integrationCS)
		integrationCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogIntegrationCS.hlsl", {}, "cs_5_0"));
	return integrationCS;
}

ID3D11ComputeShader* ExponentialHeightFog::GetFarIntegrationCS()
{
	if (!farIntegrationCS)
		farIntegrationCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\ExponentialHeightFog\\VolumetricFogIntegrationCS.hlsl", { { "VOLUMETRIC_FOG_FAR_GRID", "" } }, "cs_5_0"));
	return farIntegrationCS;
}

void ExponentialHeightFog::Prepass()
{
	if (!settings.enabled || !settings.volumetricFogEnabled || settings.volumetricFogExtinctionScale <= 0.0f) {
		ReleaseVolumetricResources();
		return;
	}

	if (IsSuppressed())
		return;

	EnsureVolumetricResources();

	if (settings.fogDensity <= 0.0f && settings.fogDensity2 <= 0.0f) {
		hasLightScatteringHistory = false;
		hasConservativeDepthHistory = false;
		hasLightScatteringFarHistory = false;
		hasConservativeDepthFarHistory = false;
		lastPrepassFrame = UINT32_MAX;
		BindIntegratedLightScattering();
		return;
	}

	ID3D11ShaderResourceView* directionalShadowLightData = globals::deferred && globals::deferred->directionalShadowLights ? globals::deferred->directionalShadowLights->srv.get() : nullptr;
	auto& lightLimitFix = globals::features::lightLimitFix;
	const bool hasLocalLightData =
		lightLimitFix.loaded &&
		lightLimitFix.lights &&
		lightLimitFix.lightIndexList &&
		lightLimitFix.lightGrid;
	auto* depthSrv = Util::GetCurrentSceneDepthSRV(true);
	auto& ibl = globals::features::ibl;
	auto& skylighting = globals::features::skylighting;
	const bool hasIBL = ibl.loaded &&
	                    ibl.settings.EnableIBL != 0 &&
	                    !ibl.IsDisabledForCurrentScene() &&
	                    ibl.envIBLTexture &&
	                    ibl.skyIBLTexture;
	const bool hasSkylighting = skylighting.loaded && skylighting.texProbeArray;

	const bool temporalReprojection = Util::GetTemporal();
	const bool temporalHistoryValid =
		temporalReprojection &&
		hasLightScatteringHistory &&
		lastPrepassFrame != UINT32_MAX &&
		globals::state->frameCount == lastPrepassFrame + 1u;
	const bool temporalHistoryValidFar =
		temporalReprojection &&
		hasLightScatteringFarHistory &&
		lastPrepassFrame != UINT32_MAX &&
		globals::state->frameCount == lastPrepassFrame + 1u;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// Depth ranges. The near volume spans [nearPlane, nearEndDepth] at full resolution;
	// the far volume continues [nearEndDepth, totalFarPlane] at quarter lattice.
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	const auto cameraData = Util::GetCameraData();
	const double nearPlane = std::max(static_cast<double>(cameraData.y), static_cast<double>(std::max(settings.volumetricFogStartDistance, 0.0f)));
	const double totalFarPlane = std::max(nearPlane + 1.0, static_cast<double>(std::max(settings.volumetricFogDistance, settings.volumetricFogStartDistance + 1.0f)));
	const double nearEndDepth = std::min(
		std::max(static_cast<double>(std::max(settings.volumetricNearGridDistance, 0.0f)), nearPlane + 1.0),
		totalFarPlane);
	const bool farGridEnabled = nearEndDepth + 1.0 < totalFarPlane;

	auto computeGridZParams = [](double a_nearPlane, double a_farPlane, uint32_t a_gridZ, float a_distributionScale) {
		const double nearWithOffset = a_nearPlane + 0.095 * 100.0;
		const double depthDistributionScale = std::max(static_cast<double>(a_distributionScale), static_cast<double>(a_gridZ) / 120.0);
		const double farExp = std::exp2(std::min(static_cast<double>(a_gridZ) / depthDistributionScale, 120.0));
		const double gridZOffset = (a_farPlane - nearWithOffset * farExp) / (a_farPlane - nearWithOffset);
		const double gridZScale = (1.0 - gridZOffset) / nearWithOffset;
		return DirectX::XMFLOAT4{
			static_cast<float>(gridZScale),
			static_cast<float>(gridZOffset),
			static_cast<float>(depthDistributionScale),
			0.0f
		};
	};

	VolumetricFogCB cb{};
	cb.gridSizeAndFlags = {
		currentGridSize.x,
		currentGridSize.y,
		currentGridSize.z,
		(directionalShadowMap && directionalShadowLightData ? 1u : 0u) |
			(depthSrv ? 2u : 0u) |
			(hasIBL ? 4u : 0u) |
			(hasSkylighting ? 8u : 0u) |
			(depthSrv && temporalHistoryValid && hasConservativeDepthHistory ? 16u : 0u) |
			(hasLocalLightData ? 32u : 0u)
	};
	cb.invGridSizeAndNearFade = {
		1.0f / static_cast<float>(currentGridSize.x),
		1.0f / static_cast<float>(currentGridSize.y),
		1.0f / static_cast<float>(currentGridSize.z),
		settings.volumetricFogNearFadeInDistance > 0.0f ? 1.0f / settings.volumetricFogNearFadeInDistance : 100000000.0f
	};
	cb.gridZParams = computeGridZParams(nearPlane, nearEndDepth, currentGridSize.z, settings.volumetricDepthDistributionScale);

	cb.farGridSizeAndFlags = {
		currentFarGridSize.x,
		currentFarGridSize.y,
		currentFarGridSize.z,
		(directionalShadowMap && directionalShadowLightData ? 1u : 0u) |
			(depthSrv ? 2u : 0u) |
			(hasIBL ? 4u : 0u) |
			(hasSkylighting ? 8u : 0u) |
			(depthSrv && temporalHistoryValidFar && hasConservativeDepthFarHistory ? 16u : 0u)
	};
	cb.farInvGridSizeAndNearFade = {
		1.0f / static_cast<float>(currentFarGridSize.x),
		1.0f / static_cast<float>(currentFarGridSize.y),
		1.0f / static_cast<float>(currentFarGridSize.z),
		settings.volumetricFogNearFadeInDistance > 0.0f ? 1.0f / settings.volumetricFogNearFadeInDistance : 100000000.0f
	};
	cb.farGridZParams = computeGridZParams(nearEndDepth, totalFarPlane, currentFarGridSize.z, settings.volumetricDepthDistributionScale);
	cb.farRange = { static_cast<float>(nearEndDepth), static_cast<float>(totalFarPlane), 0.0f, 0.0f };

	cb.clipToWorld = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Invert();

	for (uint32_t i = 0; i < std::size(cb.frameJitterOffsets); i++) {
		const uint32_t temporalFrame = (globals::state->frameCount - i) & 1023u;
		cb.frameJitterOffsets[i] = {
			temporalReprojection ? Halton(temporalFrame, 2) : 0.5f,
			temporalReprojection ? Halton(temporalFrame, 3) : 0.5f,
			temporalReprojection ? Halton(temporalFrame, 5) : 0.5f,
			0.0f
		};
	}
	cb.historyParameters = {
		temporalHistoryValid ? std::clamp(settings.volumetricHistoryWeight, 0.0f, 0.99f) : 0.0f,
		static_cast<float>(std::clamp(settings.volumetricHistoryMissSampleCount, 1u, 16u)),
		0.0f,
		0.0f
	};
	cb.jitterParameters = {
		temporalReprojection ? std::max(settings.volumetricSampleJitterMultiplier, 0.0f) : 0.0f,
		static_cast<float>(globals::state->frameCount % 8u),
		0.0f,
		0.0f
	};
	volumetricFogCB->Update(cb);

	auto context = globals::d3d::context;
	ID3D11Buffer* cbuffers[1]{ volumetricFogCB->CB() };
	context->CSSetConstantBuffers(0, 1, cbuffers);

	ID3D11Buffer* sharedBuffers[2]{ globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	context->CSSetConstantBuffers(5, 2, sharedBuffers);

	ID3D11Buffer* frameBuffers[1]{ *globals::game::perFrame.get() };
	context->CSSetConstantBuffers(12, 1, frameBuffers);

	ID3D11SamplerState* samplers[2]{ linearSampler.get(), shadowSampler.get() };
	context->CSSetSamplers(0, 2, samplers);

	context->CSSetShaderResources(17, 1, &depthSrv);
	ID3D11ShaderResourceView* skylightingSrv = hasSkylighting ? skylighting.texProbeArray->srv.get() : nullptr;
	ID3D11ShaderResourceView* iblSrvs[2]{
		hasIBL ? ibl.envIBLTexture->srv.get() : nullptr,
		hasIBL ? ibl.skyIBLTexture->srv.get() : nullptr
	};
	context->CSSetShaderResources(50, 1, &skylightingSrv);
	context->CSSetShaderResources(76, 2, iblSrvs);

	struct VolumetricPassDesc
	{
		DirectX::XMUINT4 gridSize;
		Texture3D* vBuffer;
		Texture2D* conservativeDepth;
		Texture2D* conservativeDepthHistory;  // may be null
		Texture3D* scattering;
		Texture3D* scatteringHistory;  // may be null
		Texture3D* integrated;
		ID3D11ComputeShader* materialSetupCS;
		ID3D11ComputeShader* conservativeDepthCS;
		ID3D11ComputeShader* lightScatteringCS;
		ID3D11ComputeShader* integrationCS;
		bool hasPrevConservativeDepth;
		const char* name;
	};

	auto runVolumetricPass = [&](const VolumetricPassDesc& p) {
		const uint32_t groupX = (p.gridSize.x + 7) / 8;
		const uint32_t groupY = (p.gridSize.y + 7) / 8;
		const uint32_t groupZ = (p.gridSize.z + 3) / 4;

		if (depthSrv) {
			ID3D11UnorderedAccessView* uavs[1]{ p.conservativeDepth->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(p.conservativeDepthCS, nullptr, 0);
			globals::profiler->BeginPass(p.name);
			context->Dispatch(groupX, groupY, 1);
			globals::profiler->EndPass();
			uavs[0] = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		}

		{
			ID3D11UnorderedAccessView* uavs[1]{ p.vBuffer->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(p.materialSetupCS, nullptr, 0);
			globals::profiler->BeginPass(p.name);
			context->Dispatch(groupX, groupY, groupZ);
			globals::profiler->EndPass();
			uavs[0] = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		}

		{
			ID3D11ShaderResourceView* srvs[5]{
				p.vBuffer->srv.get(),
				directionalShadowMap.get(),
				p.scatteringHistory ? p.scatteringHistory->srv.get() : nullptr,
				p.conservativeDepth->srv.get(),
				p.hasPrevConservativeDepth && p.conservativeDepthHistory ? p.conservativeDepthHistory->srv.get() : nullptr
			};
			ID3D11ShaderResourceView* localLightSrvs[3]{
				hasLocalLightData ? lightLimitFix.lights->srv.get() : nullptr,
				hasLocalLightData ? lightLimitFix.lightIndexList->srv.get() : nullptr,
				hasLocalLightData ? lightLimitFix.lightGrid->srv.get() : nullptr
			};
			ID3D11UnorderedAccessView* uavs[1]{ p.scattering->uav.get() };
			context->CSSetShaderResources(0, 5, srvs);
			context->CSSetShaderResources(35, 3, localLightSrvs);
			context->CSSetShaderResources(98, 1, &directionalShadowLightData);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(p.lightScatteringCS, nullptr, 0);
			globals::profiler->BeginPass(p.name);
			context->Dispatch(groupX, groupY, groupZ);
			globals::profiler->EndPass();
			uavs[0] = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		}

		{
			ID3D11ShaderResourceView* srvs[1]{ p.scattering->srv.get() };
			ID3D11UnorderedAccessView* uavs[1]{ p.integrated->uav.get() };
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(p.integrationCS, nullptr, 0);
			globals::profiler->BeginPass(p.name);
			context->Dispatch(groupX, groupY, 1);
			globals::profiler->EndPass();
		}
	};

	runVolumetricPass({ currentGridSize,
		vBufferA.get(),
		conservativeDepth.get(),
		temporalHistoryValid && hasConservativeDepthHistory ? conservativeDepthHistory.get() : nullptr,
		lightScattering.get(),
		temporalHistoryValid ? lightScatteringHistory.get() : nullptr,
		integratedLightScattering.get(),
		GetMaterialSetupCS(),
		GetConservativeDepthCS(),
		GetLightScatteringCS(),
		GetIntegrationCS(),
		temporalHistoryValid && hasConservativeDepthHistory,
		"ExponentialHeightFog::NearVolume" });

	if (farGridEnabled) {
		runVolumetricPass({ currentFarGridSize,
			vBufferAFar.get(),
			conservativeDepthFar.get(),
			temporalHistoryValidFar && hasConservativeDepthFarHistory ? conservativeDepthFarHistory.get() : nullptr,
			lightScatteringFar.get(),
			temporalHistoryValidFar ? lightScatteringFarHistory.get() : nullptr,
			integratedLightScatteringFar.get(),
			GetFarMaterialSetupCS(),
			GetFarConservativeDepthCS(),
			GetFarLightScatteringCS(),
			GetFarIntegrationCS(),
			temporalHistoryValidFar && hasConservativeDepthFarHistory,
			"ExponentialHeightFog::FarVolume" });
	} else {
		const float clearValue[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
		context->ClearUnorderedAccessViewFloat(integratedLightScatteringFar->uav.get(), clearValue);
		hasLightScatteringFarHistory = false;
		hasConservativeDepthFarHistory = false;
	}

	ID3D11ShaderResourceView* nullSrvs[5]{ nullptr, nullptr, nullptr, nullptr, nullptr };
	ID3D11ShaderResourceView* nullDepthSrv[1]{ nullptr };
	ID3D11UnorderedAccessView* nullUav[1]{ nullptr };
	ID3D11SamplerState* nullSamplers[2]{ nullptr, nullptr };
	ID3D11Buffer* nullCb[1]{ nullptr };
	context->CSSetShaderResources(0, 5, nullSrvs);
	context->CSSetShaderResources(17, 1, nullDepthSrv);
	context->CSSetShaderResources(35, 3, nullSrvs);
	context->CSSetShaderResources(50, 1, nullDepthSrv);
	context->CSSetShaderResources(76, 2, nullSrvs);
	context->CSSetShaderResources(98, 1, nullSrvs);
	context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
	context->CSSetSamplers(0, 2, nullSamplers);
	context->CSSetConstantBuffers(0, 1, nullCb);
	context->CSSetShader(nullptr, nullptr, 0);

	if (temporalReprojection) {
		context->CopyResource(lightScatteringHistory->resource.get(), lightScattering->resource.get());
		hasLightScatteringHistory = true;
		if (depthSrv) {
			context->CopyResource(conservativeDepthHistory->resource.get(), conservativeDepth->resource.get());
			hasConservativeDepthHistory = true;
		} else {
			hasConservativeDepthHistory = false;
		}
		if (farGridEnabled) {
			context->CopyResource(lightScatteringFarHistory->resource.get(), lightScatteringFar->resource.get());
			hasLightScatteringFarHistory = true;
			if (depthSrv) {
				context->CopyResource(conservativeDepthFarHistory->resource.get(), conservativeDepthFar->resource.get());
				hasConservativeDepthFarHistory = true;
			} else {
				hasConservativeDepthFarHistory = false;
			}
		} else {
			hasLightScatteringFarHistory = false;
			hasConservativeDepthFarHistory = false;
		}
	} else {
		hasLightScatteringHistory = false;
		hasConservativeDepthHistory = false;
		hasLightScatteringFarHistory = false;
		hasConservativeDepthFarHistory = false;
	}

	lastPrepassFrame = globals::state->frameCount;
	BindIntegratedLightScattering();
}

void ExponentialHeightFog::RegisterWeatherVariables()
{
	if (globals::features::effects11.loaded) {
		auto& enb = globals::features::effects11;
		if (enb.enableEffect) {
			return;
		}
	}

	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetOrCreateFeatureRegistry(GetShortName());
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Start Distance",
		"startDistance",
		"Start distance of the fog, from the camera",
		&settings.startDistance,
		0.0f,
		0.0f, 100000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height",
		"fogHeight",
		"Base height of the fog effect",
		&settings.fogHeight,
		0.0f,
		-22000.0f, 22000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height Falloff",
		"fogHeightFalloff",
		"Height density factor controls how the density increases as height decreases",
		&settings.fogHeightFalloff,
		0.2f,
		0.001f, 2.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Fog Inscattering Color",
		"fogInscatteringColor",
		"Color added to the fog inscattering contribution",
		&settings.fogInscatteringColor,
		float4{ 0.0f, 0.0f, 0.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Original Fog Color Amount",
		"originalFogColorAmount",
		"Amount of the original fog color added to fog inscattering",
		&settings.originalFogColorAmount,
		1.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Density",
		"fogDensity",
		"Overall density of the fog",
		&settings.fogDensity,
		0.02f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height 2",
		"fogHeight2",
		"Base height of the second fog layer",
		&settings.fogHeight2,
		0.0f,
		-22000.0f, 22000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height Falloff 2",
		"fogHeightFalloff2",
		"Height density factor of the second fog layer controlling how the density increases as height decreases",
		&settings.fogHeightFalloff2,
		0.2f,
		0.001f, 2.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Density 2",
		"fogDensity2",
		"Overall density of the second fog layer",
		&settings.fogDensity2,
		0.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Directional Inscattering Multiplier",
		"directionalInscatteringMultiplier",
		"Multiplier for directional light inscattering",
		&settings.directionalInscatteringMultiplier,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Sunlight Attenuation Amount",
		"sunlightAttenuationAmount",
		"Amount of fog attenuation applied to direct sunlight",
		&settings.sunlightAttenuationAmount,
		1.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Directional Inscattering Anisotropy",
		"directionalInscatteringAnisotropy",
		"Henyey-Greenstein asymmetry parameter. Positive = forward scattering, 0 = isotropic, negative = back scattering.",
		&settings.directionalInscatteringAnisotropy,
		0.2f,
		-0.99f, 0.99f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Inscattering Cubemap Tint",
		"inscatteringTint",
		"RGB tint for the inscattering cubemap with alpha for intensity",
		&settings.inscatteringTint,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"respectVanillaFogFade",
		"Apply Vanilla Fade",
		"Apply vanilla fade brightness to exponential height fog",
		(bool*)&settings.respectVanillaFogFade,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"disableVanillaFog",
		"Disable Vanilla Fog",
		"Disables vanilla fog entirely, only exponential height fog is applied",
		(bool*)&settings.disableVanillaFog,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"volumetricFogEnabled",
		"Enable Volumetric Fog",
		"Enables froxel-based volumetric fog for exponential height fog",
		(bool*)&settings.volumetricFogEnabled,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric View Distance",
		"volumetricFogDistance",
		"Maximum distance covered by exponential height volumetric fog",
		&settings.volumetricFogDistance,
		60000.0f,
		1000.0f, 200000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Near Grid Distance",
		"volumetricNearGridDistance",
		"Distance covered by the full-resolution near volume; a quarter-lattice far volume covers the rest",
		&settings.volumetricNearGridDistance,
		8000.0f,
		256.0f, 50000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Fog Noise Scale",
		"volumetricFogNoiseScale",
		"Spatial frequency of the volumetric fog noise field (0 = disabled)",
		&settings.volumetricFogNoiseScale,
		0.0f,
		0.0f, 0.01f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Fog Noise Threshold",
		"volumetricFogNoiseThreshold",
		"Soft cutoff that carves clumps out of the volumetric fog noise field",
		&settings.volumetricFogNoiseThreshold,
		0.5f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float3Variable>(
		"Volumetric Fog Noise Velocity",
		"volumetricFogNoiseVelocity",
		"Animation drift of the volumetric fog noise field",
		&settings.volumetricFogNoiseVelocity,
		float3{ 0.0f, 0.0f, 0.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Start Distance",
		"volumetricFogStartDistance",
		"Start distance of volumetric fog from the camera",
		&settings.volumetricFogStartDistance,
		0.0f,
		0.0f, 200000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Near Fade In Distance",
		"volumetricFogNearFadeInDistance",
		"Distance over which volumetric fog fades in near the camera",
		&settings.volumetricFogNearFadeInDistance,
		1000.0f,
		0.0f, 20000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Extinction Scale",
		"volumetricFogExtinctionScale",
		"Scale applied to volumetric fog extinction",
		&settings.volumetricFogExtinctionScale,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Scattering Distribution",
		"volumetricFogScatteringDistribution",
		"Henyey-Greenstein scattering distribution for volumetric fog",
		&settings.volumetricFogScatteringDistribution,
		0.2f,
		-0.9f, 0.9f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Directional Scattering Intensity",
		"volumetricDirectionalScatteringIntensity",
		"Scale applied to volumetric fog directional light scattering",
		&settings.volumetricDirectionalScatteringIntensity,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Volumetric Albedo",
		"volumetricFogAlbedo",
		"Volumetric fog albedo color",
		&settings.volumetricFogAlbedo,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Volumetric Emissive",
		"volumetricFogEmissive",
		"Volumetric fog emissive color",
		&settings.volumetricFogEmissive,
		float4{ 0.0f, 0.0f, 0.0f, 0.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Sky Lighting Intensity",
		"volumetricSkyLightingIntensity",
		"Scale applied to volumetric fog sky lighting",
		&settings.volumetricSkyLightingIntensity,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Local Light Scattering Intensity",
		"volumetricLocalLightScatteringIntensity",
		"Scale applied to volumetric fog local light scattering",
		&settings.volumetricLocalLightScatteringIntensity,
		1.0f,
		0.0f, 100.0f));
}
#undef I18N_KEY_PREFIX
