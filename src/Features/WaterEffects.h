#pragma once

#include <winrt/base.h>

/** @brief Enhances water rendering with realistic caustics and underwater lighting effects. */
struct WaterEffects : Feature
{
public:
	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;
	virtual inline std::string GetName() override { return "Water Effects"; }
	virtual std::string GetDisplayName() override { return T("feature.water_effects.name", "Water Effects"); }
	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "WaterEffects"; }
	virtual inline std::string_view GetShaderDefineName() override { return "WATER_EFFECTS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }

	/** @brief Returns a summary description and list of key features for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.water_effects.description", "Water Effects enhances water rendering with realistic caustics and underwater lighting effects.\nThis feature adds dynamic light patterns and improved water visual quality."),
			{ T("feature.water_effects.key_feature_1", "Realistic water caustics"),
				T("feature.water_effects.key_feature_2", "Enhanced underwater lighting"),
				T("feature.water_effects.key_feature_3", "Dynamic light patterns on water surfaces"),
				T("feature.water_effects.key_feature_4", "Improved water visual fidelity"),
				T("feature.water_effects.key_feature_5", "Atmospheric underwater effects") } };
	};

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	/** @brief Loads the water caustics DDS texture from disk. */
	virtual void SetupResources() override;

	/** @brief Binds the caustics texture SRV to the pixel shader for the current frame. */
	virtual void Prepass() override;

	virtual bool IsCore() const override { return true; };

	/** @brief User settings. */
	struct Settings
	{
		bool EnableCaustics = true;       ///< caustics on the light that reaches the bed (terrain, objects, grass)
		bool EnableParallax = true;       ///< ray-marched relief of the water's normal maps
		float ParallaxFadeStart = 30.0f;  ///< m: the parallax starts to fade out beyond this distance
		float ParallaxFadeEnd = 90.0f;    ///< m: and is gone beyond this one
	};

	Settings settings;

	/** @brief The constants for the shared feature buffer; matches WaterEffectsSettings in SharedData.hlsli. */
	struct alignas(16) SharedSettings
	{
		float4 Params;  // x caustics on (1/0), y/z parallax fade start/end distance (units; z = 0: parallax off), w unused
	};
	STATIC_ASSERT_ALIGNAS_16(SharedSettings);

	/** @brief Render thread: the constants for the shared feature buffer. */
	SharedSettings GetCommonBufferData() const;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
};
