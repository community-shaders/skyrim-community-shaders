#pragma once

#include "Buffer.h"

struct ExponentialHeightFog : Feature
{
	virtual bool SupportsVR() override { return true; };
	virtual inline std::string GetName() override { return "Exponential Height Fog"; }
	virtual inline std::string GetShortName() override { return "ExponentialHeightFog"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Exponential Height Fog adds a realistic fog effect that increases in density with height, enhancing atmospheric depth and immersion in the game environment.",
			{
				"Added exponential height fog effect",
				"Adapted to vanilla fog settings",
				"Creates atmospheric depth",
			}
		};
	}

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

	struct UInt4
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t z = 0;
		uint32_t w = 0;
	};
	STATIC_ASSERT_ALIGNAS_16(UInt4);

	struct Settings
	{
		uint enabled = 0;
		uint useDynamicCubemaps = 0;
		float startDistance = 0.0f;
		float fogHeight = 0.0f;
		float fogHeightFalloff = 0.2f;
		float fogDensity = 0.02f;
		float directionalInscatteringMultiplier = 1.0f;
		float directionalInscatteringAnisotropy = 0.7f;
		float4 inscatteringTint = { 1.0f, 1.0f, 1.0f, 1.0f };
		float cubemapMipLevel = 3.0f;
		float sunlightAttenuationAmount = 1.0f;
		uint respectVanillaFogFade = 0;
		uint disableVanillaFog = 0;
		float4 fogInscatteringColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		float originalFogColorAmount = 1.0f;
		uint volumetricFogEnabled = 0;
		uint volumetricGridPixelSize = 16;
		uint volumetricGridSizeZ = 64;
		float volumetricFogDistance = 60000.0f;
		float volumetricFogStartDistance = 0.0f;
		float volumetricFogNearFadeInDistance = 1000.0f;
		float volumetricFogExtinctionScale = 1.0f;
		float volumetricFogScatteringDistribution = 0.2f;
		float3 volumetricPad0;
		float4 volumetricFogAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
		float4 volumetricFogEmissive = { 0.0f, 0.0f, 0.0f, 0.0f };
		float volumetricDirectionalScatteringIntensity = 1.0f;
		float volumetricShadowBias = 0.002f;
		float volumetricDepthDistributionScale = 32.0f;
		float volumetricSkyLightingIntensity = 1.0f;
	} settings;
	STATIC_ASSERT_ALIGNAS_16(Settings);

private:
	struct VolumetricFogCB
	{
		UInt4 gridSizeAndFlags = {};
		float4 invGridSizeAndNearFade = {};
		float4 gridZParams = {};
		float4x4 clipToWorld[2] = {};
		float4 frameJitterAndHistory[4] = {};
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
	UInt4 currentGridSize = {};
	bool hasLightScatteringHistory = false;
	bool hasConservativeDepthHistory = false;
};
