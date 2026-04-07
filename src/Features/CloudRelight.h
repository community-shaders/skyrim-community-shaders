#pragma once

struct CloudRelight : Feature
{
	inline std::string GetName() override { return "Cloud Relight"; }
	inline std::string GetShortName() override { return "CloudRelight"; }
	inline std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	inline std::string GetFeatureModLink() override { return ""; }
	inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Physically-based relighting of vanilla cloud textures using the directional sun light "
			"and the Cloud Shadows cubemap, adding silver-lining and forward-scattering effects.",
			{
				"Silver-lining and forward-scattering on vanilla clouds",
				"Cloud self-shadowing via Cloud Shadows cubemap",
				"Adjustable vanilla vs. relit blend",
			}
		};
	}

	inline bool SupportsVR() override { return true; }
	inline std::string_view GetShaderDefineName() override { return "CLOUD_RELIGHT"; }
	inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	void DataLoaded() override;
	void RestoreDefaultSettings() override;
	void LoadSettings(json& o_json) override;
	void SaveSettings(json& o_json) override;
	void DrawSettings() override;

	struct alignas(16) Settings
	{
		uint32_t enabled = 1;
		float cloudRelightMix = 1.f;
		float cloudOriginalMix = 0.75f;
		float silverLiningMix = 1.f;
		float silverLiningSpread = 0.f;
		float3 pad0;
	};

	Settings settings;
};
