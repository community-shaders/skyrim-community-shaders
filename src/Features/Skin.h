#pragma once

struct Skin : Feature
{
	static Skin* GetSingleton()
	{
		static Skin singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Advanced Skin"; }
	virtual inline std::string GetShortName() override { return "Skin"; }
	virtual inline std::string_view GetShaderDefineName() override { return "CS_SKIN"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
	{
		return t == RE::BSShader::Type::Lighting;
	};

	virtual inline bool SupportsVR() { return true; }

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void Prepass() override;

	virtual void SetupResources() override;

	void ReloadSkinDetail();

	struct Settings
	{
		bool EnableSkin = true;
		float SkinMainRoughness = 0.7f;
		float SkinSecondRoughness = 0.35f;
		float SkinSpecularTexMultiplier = 1.0f;
		float SecondarySpecularStrength = 0.15f;
		float F0 = 0.0278f;
		float PhysicalMainRoughnessMultiplier = 1.3f;
		float PhysicalSecondRoughnessMultiplier = 0.75f;
		float PhysicalSpecularStrength = 1.0f;
		float ExtraEdgeRoughness = 0.25f;
		bool EnableSkinDetail = true;
		float SkinDetailStrength = 0.5f;
		float SkinDetailTiling = 20.0f;
		float BodyTilingMultiplier = 2.0f;
		bool ApplySpecularToWetness = false;
		float ExtraSkinWetness = 0.0f;
		float Translucency = 0.1f;
		float sssWidth = 0.2f;
		float thicknessMult = 20.0f;
		bool UseSSS = true;
		bool UseCalcThickness = false;
		float FuzzStrength = 1.0f;
		float FuzzRoughness = 0.35f;
		float FuzzF0 = 0.045f;
	} settings;

	struct alignas(16) SkinData
	{
		float4 skinParams;
		float4 skinParams2;
		float4 skinDetailParams;
		float4 sssParams;
		float4 fuzzParams;
		float4 physicalParams;
		uint ApplySpecularToWetness;
		uint pad0[3];
	};

	eastl::unique_ptr<Texture2D> texSkinDetail = nullptr;

	SkinData GetCommonBufferData();
};