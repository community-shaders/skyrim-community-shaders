#pragma once

struct CloudRelight : Feature
{
	struct alignas(16) Settings
	{
		uint enabled = true;
		float cloudRelightMix = 1.0f;
		float cloudOriginalMix = 0.5f;
		float silverLiningMix = 1.0f;

		float silverLiningSpread = 0.0f;
		float pad[3] = {};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual inline std::string GetName() override { return "Cloud Relight"; }
	virtual inline std::string GetShortName() override { return "CloudRelight"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	virtual inline std::string_view GetShaderDefineName() override { return "CLOUD_RELIGHT"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Sky; }
	virtual bool SupportsVR() override { return true; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Relights vanilla cloud textures using directional sunlight, cloud self-shadowing, and silver-lining phase lighting.",
			{ "Vanilla cloud color and relit cloud color blending",
				"Silver-lining and forward-scattering phase lighting",
				"Directional cloud self-shadowing" }
		};
	}

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	Settings GetCommonBufferData() const;
};
