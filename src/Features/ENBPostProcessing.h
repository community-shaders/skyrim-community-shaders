#pragma once

struct ENBPostProcessing : Feature
{
public:
	struct Settings
	{
		bool Enabled = false;
		std::string EffectPath = "";
	};

	Settings settings;

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
		uint Enable;
		float3 pad;

		float GradientIntensity;
		float GradientDesaturation;
		float GradientTopIntensity;
		float GradientTopCurve;

		float3 GradientTopColorFilter;
		float pad0;

		float GradientMiddleIntensity;
		float GradientMiddleCurve;
		float2 pad1;

		float3 GradientMiddleColorFilter;
		float pad2;

		float GradientHorizonIntensity;
		float GradientHorizonCurve;
		float2 pad3;

		float3 GradientHorizonColorFilter;
		float pad4;

		float CloudsIntensity;
		float CloudsCurve;
		float CloudsDesaturation;
		float CloudsOpacity;

		float3 CloudsColorFilter;
		float pad5;

		float CloudsEdgeClamp;
		float CloudsEdgeIntensity;
		float CloudsEdgeFadeRange;
		float CloudsEdgeMoonMultiplier;

		float ColorPow;
		float3 pad8;

		float IBLMultiplicativeAmount;
		float3 pad9;

		float VolumetricRaysIntensity;
		float VolumetricRaysRangeFactor;
		float VolumetricRaysDesaturation;
		float pad12;

		float3 VolumetricRaysColorFilter;
		float pad13;
	};

	bool enableEffect = false;

	PerFrame GetCommonBufferData();

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	void OverrideWeather(RE::Sky* a_sky);
	void CheckCommonData();
	struct DirectionalAmbientColors
	{
		RE::NiColor directionalAmbientColors[3][2];
	};
	void OverrideAmbientLighting(DirectionalAmbientColors& DirectionalAmbientColors);
	virtual void PostPostLoad() override;
};