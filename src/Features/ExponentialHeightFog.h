#pragma once

struct ExponentialHeightFog : Feature
{
private:
	static constexpr std::string_view MOD_ID = "180146";

public:
	virtual bool SupportsVR() override { return true; };
	virtual inline std::string GetName() override { return "Exponential Height Fog"; }
	virtual inline std::string GetShortName() override { return "ExponentialHeightFog"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
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

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	void RegisterWeatherVariables() override;

	struct alignas(16) Settings
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
		float3 pad;
	} settings;
	static_assert(sizeof(Settings) == sizeof(float4) * 6, "Settings must match HLSL ExponentialHeightFogSettings.");
};
