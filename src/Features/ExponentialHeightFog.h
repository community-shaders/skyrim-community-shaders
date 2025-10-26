#pragma once

struct ExponentialHeightFog : Feature
{
	virtual bool SupportsVR() override { return true; };
	virtual inline std::string GetName() override { return "Exponential Height Fog"; }
	virtual inline std::string GetShortName() override { return "ExponentialHeightFog"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
    virtual std::string_view GetCategory() const override { return "Lighting"; }

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

	struct alignas(16) Settings
	{
		uint enabled = 1;
		uint useDynamicCubemaps = 0;
		float startDistance = 0.0f;
		float fogHeight = 0.0f;
		float fogHeightFalloff = 0.2f;
		float fogDensity = 0.02f;
		float directionalInscatteringMultiplier = 1.0f;
		float directionalInscatteringExponent = 2.0f;
		float4 inscatteringTint = { 1.0f, 1.0f, 1.0f, 1.0f };
		float cubemapMipLevel = 2.0f;
		float pad[3];
	} settings;
};