#pragma once

struct LinearLighting : Feature
{
	static LinearLighting* GetSingleton()
	{
		static LinearLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Linear Lighting"; }
	virtual inline std::string GetShortName() override { return "LinearLighting"; }
	virtual std::string_view GetCategory() const override { return "Lighting"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Linear Lighting does internal color space conversion to improve lighting calculation accuracy.",
			{ "Customizable gamma correction",
				"Corrects lighting calculations",
				"Makes PBR really work" }
		};
	}

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	struct Settings
	{
		uint enableLinearLighting = true;
		uint enableGammaCorrection = true;
		float lightGamma = 1.8f;
		float colorGamma = 2.2f;
		float emitColorGamma = 2.2f;
		float glowmapGamma = 2.2f;
		float ambientGamma = 1.8f;
		float fogGamma = 2.2f;
		float fogAlphaGamma = 1.0f;
		float effectGamma = 1.8f;
		float effectAlphaGamma = 1.0f;
		float skyGamma = 1.8f;
		float waterGamma = 1.8f;
		float vlGamma = 1.8f;

		// Lighting multipliers
		float vanillaDiffuseMult = 0.32f;
		float vanillaSpecularMult = 0.32f;
		float grassDiffuseMult = 0.32f;
		float grassSpecularMult = 0.32f;
		float vanillaDiffuseColorMult = 1.5f;
		float lightMult = 1.0f;
		float directionalLightMult = 1.0f;
		float pointLightMult = 1.0f;
		float emitColorMult = 1.0f;
		float glowmapMult = 1.0f;

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
		uint enableGammaCorrection;
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
		float vanillaDiffuseMult;
		float vanillaSpecularMult;
		float grassDiffuseMult;
		float grassSpecularMult;
		float vanillaDiffuseColorMult;
		float lightMult;
		float directionalLightMult;
		float pointLightMult;
		float emitColorMult;
		float glowmapMult;
		float effectLightingMult;
		float membraneEffectMult;
		float bloodEffectMult;
		float projectedEffectMult;
		float deferredEffectMult;
		float otherEffectMult;
	};

	uint isDirLightLinear = false;
	float dirLightMult = 1.0f;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void Prepass() override;

	PerFrameData GetCommonBufferData();

	RE::NiColor ColorToLinear(RE::NiColor inColor, float gamma);
};
