#pragma once

struct ENBPostProcessing : Feature
{
public:
	virtual inline std::string GetName() override { return "ENB Post Processing"; }
	virtual inline std::string GetShortName() override { return "ENBPostProcessing"; }
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"ENB Post Processing provides a framework for loading and executing ENBSeries-compatible FX effect files.\n"
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
		float3 CloudsColorFilter;
		uint32_t Enable;

		float3 VolumetricRaysColorFilter;
		uint32_t EnableSky;

		float CloudsIntensity;
		float CloudsCurve;
		float CloudsDesaturation;
		float CloudsOpacity;

		float CloudsEdgeIntensity;
		float CloudsEdgeMoonMultiplier;
		float VolumetricRaysRangeFactor;
		float VolumetricRaysDesaturation;

		float SkyScaleIntensity;
		float ColorPow;
		float pad0;
		float pad1;
	};

	bool enableEffect = false;

	PerFrame GetCommonBufferData();

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void Prepass() override;
	void OverrideWeather(RE::Sky* a_sky);
	void CheckCommonData();
	void OverridePointLightColor(float3& a_color);
	void OverrideVolumetricLighting(RE::NiColorA& a_color);
	struct DirectionalAmbientColors
	{
		RE::NiColor directionalAmbientColors[3][2];
	};
	void OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors);
	void ModifySky(RE::BSRenderPass* Pass);
	virtual void PostPostLoad() override;
};