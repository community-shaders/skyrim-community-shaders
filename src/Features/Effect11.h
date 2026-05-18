#pragma once

#include "Buffer.h"

#include <memory>

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

		uint _pad1;
		uint EnableCloudsLightingFromMoon;
		uint ScatteringColorHDRWeighting;
		float SkyScatteringAtmosphereThickness;

		float SkyScatteringHorizonRange;
		float SkyScatteringIntensity;
		float SkyScatteringAmount;
		float SkyScatteringDustVolume;

		float SkyScatteringDustDensity;
		float SkyScatteringDustDarkening;
		float SkyScatteringShadowAmount;
		float SkyScatteringColorFromSun;

		float3 SkyScatteringColor;
		float SkyScatteringAirGlowIntensity;

		float SkyScatteringAirGlowRange;
		float SkyScatteringSunGlowIntensity;
		float SkyScatteringSunGlowRange;
		float SkyScatteringMoonGlowAmount;

		float SkyScatteringMoonGlowRange;
		float SkyScatteringCloudsLightingSunMinIntensity;
		float SkyScatteringCloudsLightingSunMultiplier;
		float SkyScatteringCloudsLightingMoonIntensity;

		uint EnableCloudsScattering;
		uint EnableVolumetricRays;
		float VolumetricRaysIntensity;
		float VolumetricRaysExtinction;

		float VolumetricRaysSkyColorAmount;
		float3 _pad0;
	};

	bool enableEffect = false;

	ID3D11PixelShader* raymarchVolumetricRaysPS = nullptr;
	ID3D11PixelShader* applyVolumetricRaysPS = nullptr;
	ID3D11ComputeShader* blurHCS = nullptr;
	ID3D11ComputeShader* blurVCS = nullptr;
	ID3D11BlendState* additiveBlendState = nullptr;

	std::unique_ptr<Texture2D> vrTexA;
	std::unique_ptr<Texture2D> vrTexB;
	std::unique_ptr<ConstantBuffer> vrBlurCB;

	PerFrame GetCommonBufferData();

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void Prepass() override;
	virtual void ClearShaderCache() override;

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
	virtual void PostPostLoad() override;
};