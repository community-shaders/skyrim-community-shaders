#pragma once

#include "Buffer.h"

struct ExponentialHeightFog : Feature
{
private:
	static constexpr std::string_view MOD_ID = "180146";

public:
	virtual bool SupportsVR() override { return true; };
	virtual inline std::string GetName() override { return "Exponential Height Fog"; }
	virtual std::string GetDisplayName() override { return T("feature.exponential_height_fog.name", "Exponential Height Fog"); }
	virtual inline std::string GetShortName() override { return "ExponentialHeightFog"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.exponential_height_fog.description", "Exponential Height Fog adds a realistic fog effect that increases in density with height, enhancing atmospheric depth and immersion in the game environment."),
			{ T("feature.exponential_height_fog.key_feature_1", "Added exponential height fog effect"),
				T("feature.exponential_height_fog.key_feature_2", "Adapted to vanilla fog settings"),
				T("feature.exponential_height_fog.key_feature_3", "Creates atmospheric depth") } };
	};

	virtual inline std::string_view GetShaderDefineName() override { return "EXP_HEIGHT_FOG"; }
	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	virtual void Prepass() override;

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	void RegisterWeatherVariables() override;
	void CaptureDirectionalShadowMap();

	struct Settings
	{
		uint enabled = 0;
		uint useDynamicCubemaps = 1;
		float startDistance = 0.0f;
		float fogHeight = 0.0f;
		float fogHeightFalloff = 0.2f;
		float fogDensity = 0.005f;
		float directionalInscatteringMultiplier = 1.0f;
		float directionalInscatteringAnisotropy = 0.2f;
		float4 inscatteringTint = { 1.0f, 1.0f, 1.0f, 1.0f };
		float cubemapMipLevel = 7.0f;
		float sunlightAttenuationAmount = 1.0f;
		uint respectVanillaFogFade = 0;
		uint disableVanillaFog = 1;
		float4 fogInscatteringColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		float originalFogColorAmount = 0.0f;
		uint volumetricFogEnabled = 0;
		uint volumetricGridPixelSize = 16;
		uint volumetricGridSizeZ = 64;
		float volumetricFogDistance = 60000.0f;
		float volumetricFogStartDistance = 0.0f;
		float volumetricFogNearFadeInDistance = 1000.0f;
		float volumetricFogExtinctionScale = 1.0f;
		float4 volumetricFogAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
		float4 volumetricFogEmissive = { 0.0f, 0.0f, 0.0f, 0.0f };
		float volumetricDirectionalScatteringIntensity = 1.0f;
		float volumetricShadowBias = 0.002f;
		float volumetricDepthDistributionScale = 8.0f;
		float volumetricSkyLightingIntensity = 1.0f;
		float volumetricFogScatteringDistribution = 0.2f;
		float volumetricHistoryWeight = 0.96f;
		uint volumetricHistoryMissSampleCount = 4;
		float volumetricSampleJitterMultiplier = 0.0f;
		float volumetricUpsampleJitterMultiplier = 1.0f;
		float volumetricLocalLightScatteringIntensity = 1.0f;
		float2 pad0;
	} settings;
	STATIC_ASSERT_ALIGNAS_16(Settings);

private:
	struct VolumetricFogCB
	{
		DirectX::XMUINT4 gridSizeAndFlags = {};
		float4 invGridSizeAndNearFade = {};
		float4 gridZParams = {};
		float4x4 clipToWorld[2] = {};
		float4 frameJitterOffsets[16] = {};
		float4 historyParameters = {};
		float4 jitterParameters = {};  // x = LightScatteringSampleJitterMultiplier, y = StateFrameIndexMod8, zw = unused
	};
	STATIC_ASSERT_ALIGNAS_16(VolumetricFogCB);

	void EnsureVolumetricResources();
	void ReleaseVolumetricResources();
	void BindIntegratedLightScattering();
	ID3D11ComputeShader* GetMaterialSetupCS();
	ID3D11ComputeShader* GetConservativeDepthCS();
	ID3D11ComputeShader* GetLightScatteringCS();
	ID3D11ComputeShader* GetIntegrationCS();

	std::unique_ptr<Texture3D> vBufferA;
	std::unique_ptr<Texture2D> conservativeDepth;
	std::unique_ptr<Texture2D> conservativeDepthHistory;
	std::unique_ptr<Texture3D> lightScattering;
	std::unique_ptr<Texture3D> lightScatteringHistory;
	std::unique_ptr<Texture3D> integratedLightScattering;
	std::unique_ptr<ConstantBuffer> volumetricFogCB;
	winrt::com_ptr<ID3D11SamplerState> linearSampler;
	winrt::com_ptr<ID3D11SamplerState> shadowSampler;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalShadowMap;
	ID3D11ComputeShader* materialSetupCS = nullptr;
	ID3D11ComputeShader* conservativeDepthCS = nullptr;
	ID3D11ComputeShader* lightScatteringCS = nullptr;
	ID3D11ComputeShader* integrationCS = nullptr;
	DirectX::XMUINT4 currentGridSize = {};
	bool hasLightScatteringHistory = false;
	bool hasConservativeDepthHistory = false;
	uint32_t lastPrepassFrame = UINT32_MAX;
};
