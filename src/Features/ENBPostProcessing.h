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
		float3 GradientTopColorFilter;
		uint32_t Enable;

		float3 GradientMiddleColorFilter;
		uint32_t EnableSky;

		float3 GradientHorizonColorFilter;
		float SkyBoostIntensity;

		float3 CloudsColorFilter;
		float GradientIntensity;

		float3 CloudsEdgeScatterColor;
		float GradientDesaturation;

		float3 VolumetricRaysColorFilter;
		float GradientTopCurve;

		float GradientMiddleCurve;
		float GradientHorizonCurve;
		float CloudsIntensity;
		float CloudsCurve;

		float CloudsDesaturation;
		float CloudsOpacity;
		float ColorPow;
		float VolumetricRaysRangeFactor;

		float VolumetricRaysDesaturation;
		float pad0;
		float pad1;
		float pad2;
	};

	bool enableEffect = false;

	PerFrame GetCommonBufferData();

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
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