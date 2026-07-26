#pragma once

#include "Buffer.h"

#include <memory>
#include <winrt/base.h>

struct Effect11 : Feature
{
public:
	virtual inline std::string GetName() override { return "Effect11"; }
	virtual inline std::string GetShortName() override { return "Effect11"; }
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Effect11 provides a framework for loading and executing ENBSeries-compatible FX effect files.\n"
			"This allows for advanced post-processing effects and visual enhancements using DirectX 11 Effect (.fx) files.",
			{ "ENBSeries-compatible FX support",
				"DirectX 11 Effect file loading",
				"Advanced post-processing pipeline",
				"Custom technique execution",
				"Dynamic UI variable system" }
		};
	}

	struct alignas(16) PerFrame
	{
		uint Enable;
		uint EnableSky;
		float ColorPow;
		float LightSpriteIntensity;

		float CloudsCurve;
		float CloudsDesaturation;
		float CloudsEdgeIntensity;
		float CloudsEdgeMoonMultiplier;

		float VolumetricRaysDesaturation;
		float3 VolumetricRaysColorFilter;

		uint UseProceduralGradientWeights;
		float ProceduralGradientWeightCurve;
		uint EnableProceduralSun;
		float ProceduralSunDiskRadiusSq;

		float ProceduralSunDiskEdgeScale;
		float ProceduralSunGlowIntensity;
		float ProceduralSunCoronaFalloff;
		float ProceduralSunCoronaScale;

		float ParticleIntensity;
		float ParticleLightingInfluence;
		float ParticleAmbientInfluence;
		float ParticlePointLightingInfluence;

		uint EnableCloudsScattering;
		uint EnableCloudsLightingFromMoon;
		float SkyScatteringIntensity;
		float SkyScatteringAmount;

		float3 SkyScatteringColor;
		float SkyScatteringColorFromSun;

		float SkyScatteringCloudsLightingSunMultiplier;
		float SkyScatteringCloudsLightingMoonIntensity;

		uint EnableVolumetricRays;
		float VolumetricRaysIntensity;
		float VolumetricRaysExtinction;
		float VolumetricRaysSkyColorAmount;

		uint EnableRain;
		float RainMotionStretch;
		float RainMotionTransparency;
		float FireIntensity;
		float FireCurve;
		uint pad0;
	};

	bool enableEffect = false;

	ID3D11PixelShader* raymarchVolumetricRaysPS = nullptr;
	ID3D11PixelShader* applyVolumetricRaysPS = nullptr;
	ID3D11ComputeShader* blurHCS = nullptr;
	ID3D11ComputeShader* blurVCS = nullptr;
	ID3D11BlendState* additiveBlendState = nullptr;
	ID3D11BlendState* alphaBlendState = nullptr;

	std::unique_ptr<Texture2D> vlTexA;
	std::unique_ptr<Texture2D> vlTexB;
	std::unique_ptr<Texture2D> vlDepthHalf;
	std::unique_ptr<ConstantBuffer> vlBlurCB;

	winrt::com_ptr<ID3D11Texture2D> raindropTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> raindropSRV;
	std::string raindropStatus;
	void LoadRaindropTexture();

	PerFrame GetCommonBufferData();

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void Prepass() override;
	virtual void ClearShaderCache() override;
	virtual void PostPostLoad() override;

	void DrawVolumetricRays();

	void OnSkyUpdateColors(RE::Sky* a_sky);
	void OverrideWeather(RE::Sky* a_sky);
	void CheckCommonData();
	void OverridePointLightColor(float3& a_color);

	struct DirectionalAmbientColors
	{
		RE::NiColor directionalAmbientColors[3][2];
	};
	void OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors);

	void ModifySky(RE::BSRenderPass* Pass);
	__declspec(noinline) void ModifyParticle(RE::BSRenderPass* Pass);
	void ParticleShaderHacks();
	bool HandleTonemapRender(RE::RENDER_TARGET a_input, RE::RENDER_TARGET a_output);
};
