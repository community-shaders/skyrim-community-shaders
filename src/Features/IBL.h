#pragma once

struct IBL : Feature
{
public:
	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	virtual inline std::string GetName() override { return "Image Based Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.ibl.name", "Image Based Lighting"); }
	virtual inline std::string GetShortName() override { return "ImageBasedLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "IBL"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.ibl.description", "Replaces the game's ambient lighting with physically-based IBL derived from cubemap spherical harmonics."),
			{ T("feature.ibl.key_feature_1", "Projects environment and sky cubemaps into spherical harmonics (SH) for irradiance"),
				T("feature.ibl.key_feature_2", "Dual IBL sources: environment cubemap (Dynamic Cubemaps) and Skyrim's native sky reflections cubemap"),
				T("feature.ibl.key_feature_3", "DALC brightness matching to keep IBL consistent with the game's ambient light levels"),
				T("feature.ibl.key_feature_4", "Configurable per-source intensity, saturation, fog mixing, and per-weather overrides"),
				T("feature.ibl.key_feature_5", "Static IBL fallback textures for out-of-world objects (e.g. inventory items)") } };
	};

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	Texture2D* envIBLTexture = nullptr;
	Texture2D* skyIBLTexture = nullptr;
	ID3D11ComputeShader* diffuseIBLCS = nullptr;

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RegisterWeatherVariables() override;

	virtual void ReflectionsPrepass() override;
	virtual void Prepass() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	struct Settings
	{
		uint EnableIBL = 0;
		uint PreserveFogLuminance = 0;
		uint UseStaticIBL = 1;
		float DALCAmount = 1.0f;
		float EnvIBLScale = 1.0f;
		float SkyIBLScale = 1.0f;
		float EnvIBLSaturation = 1.0f;
		float SkyIBLSaturation = 1.0f;
		float FogAmount = 0.0f;
		uint DALCMode = 2;  // 0: Luminance Ratio, 1: Color Ratio, 2: DALC + Sky, 3: DALC + Sky (Directional)
		uint DisableInInteriors = 1;
		float pad1 = 0.0f;
	} settings;

	eastl::unique_ptr<Texture2D> staticDiffuseIBLTexture = nullptr;
	eastl::unique_ptr<Texture2D> staticSpecularIBLTexture = nullptr;

	Settings GetCommonBufferData() const;
	ID3D11ComputeShader* GetDiffuseIBLCS();
};
