#pragma once

struct LinearLighting : Feature
{
	static LinearLighting* GetSingleton()
	{
		static LinearLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Linear Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.linear_lighting.name", "Linear Lighting"); }
	virtual inline std::string GetShortName() override { return "LinearLighting"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.linear_lighting.description", "Linear Lighting does internal color space conversion to improve lighting calculation accuracy."),
			{ T("feature.linear_lighting.key_feature_1", "Customizable gamma correction"),
				T("feature.linear_lighting.key_feature_2", "Corrects lighting calculations"),
				T("feature.linear_lighting.key_feature_3", "Makes PBR really work") } };
	};

	virtual bool IsCore() const override { return true; };

	struct Settings
	{
		uint enableLinearLighting = false;
		float lightGamma = 1.8f;
		float colorGamma = 1.8f;
		float emitColorGamma = 1.8f;
		float glowmapGamma = 1.8f;
		float ambientGamma = 1.8f;
		float fogGamma = 1.97f;
		float fogAlphaGamma = 1.8f;
		float effectGamma = 1.4f;
		float effectAlphaGamma = 1.55f;
		float skyGamma = 1.8f;
		float waterGamma = 1.8f;
		float vlGamma = 1.8f;

		// Lighting multipliers
		float vanillaDiffuseColorMult = 1.0f;
		float directionalLightMult = 1.0f;
		float pointLightMult = 1.0f;
		float ambientMult = 1.0f;
		float emitColorMult = 1.0f;
		float glowmapMult = 0.66f;

		// Effect multipliers
		float effectLightingMult = 0.32f;
		float membraneEffectMult = 1.0f;
		float bloodEffectMult = 1.0f;
		float projectedEffectMult = 1.0f;
		float deferredEffectMult = 1.0f;
		float otherEffectMult = 1.0f;
	} settings;

	struct alignas(16) PerFrameData
	{
		uint enableLinearLighting;
		uint isDirLightLinear;
		float dirLightMult;
		float lightGamma;
		float colorGamma;
		float emitColorGamma;
		float glowmapGamma;
		float ambientGamma;
		float fogGamma;
		float fogAlphaGamma;
		float effectGamma;
		float effectAlphaGamma;
		float skyGamma;
		float waterGamma;
		float vlGamma;
		float vanillaDiffuseColorMult;
		float directionalLightMult;
		float pointLightMult;
		float ambientMult;
		float emitColorMult;
		float glowmapMult;
		float effectLightingMult;
		float membraneEffectMult;
		float bloodEffectMult;
		float projectedEffectMult;
		float deferredEffectMult;
		float otherEffectMult;
		uint pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrameData);

	struct alignas(16) PerGeometryData
	{
		float emissiveMult;
		float pad0[3];
	};

	ConstantBuffer* PerGeometryCB = nullptr;

	uint isDirLightLinear = false;
	float dirLightMult = 1.0f;

	/** @brief Draws the ImGui settings UI for gamma correction and lighting multiplier configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	/** @brief Reads the directional light multiplier from ImageSpaceManager during the prepass. */
	virtual void Prepass() override;
	/** @brief Installs the BSLightingShader geometry setup hook. */
	virtual void PostPostLoad() override;

	/** @brief Creates the per-geometry constant buffer for emissive multiplier data. */
	virtual void SetupResources() override;

	/** @brief Populates and returns the per-frame constant buffer data with gamma and multiplier settings. */
	PerFrameData GetCommonBufferData();

	/**
	 * @brief Converts an NiColor from gamma space to linear space using the specified gamma value.
	 * @param inColor The input color in gamma space.
	 * @param gamma The gamma exponent to apply.
	 * @return The color converted to linear space.
	 */
	RE::NiColor ColorToLinear(RE::NiColor inColor, float gamma);

	/**
	 * @brief Uploads emissive multiplier data to the per-geometry constant buffer during shader setup.
	 * @param a_pass The render pass whose lighting properties to read.
	 */
	void BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass);

	/** @brief Contains the BSLightingShader geometry setup hook implementation. */
	struct Hooks;
};
