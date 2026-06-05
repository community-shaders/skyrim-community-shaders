#pragma once

#include "I18n/I18n.h"

struct Skin : Feature
{
	static Skin* GetSingleton()
	{
		static Skin singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Advanced Skin"; }
	virtual inline std::string GetDisplayName() override { return T("feature.skin.name", "Advanced Skin"); }
	virtual inline std::string GetShortName() override { return "Skin"; }
	virtual inline std::string_view GetShaderDefineName() override { return "CS_SKIN"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kCharacters; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.skin.description", "Advanced Skin enhances character skin rendering with multiple techniques."),
			{ T("feature.skin.key_feature_1", "Physically-based dual specular lobes for realistic skin highlights"),
				T("feature.skin.key_feature_2", "Tiled skin detail textures for enhanced realism"),
				T("feature.skin.key_feature_3", "Extra texture support for roughness, translucency, and wetness"),
				T("feature.skin.key_feature_4", "Reworked wetness system for dynamic skin effects") }
		};
	}
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
	virtual void PostPostLoad() override;

	virtual void SetupResources() override;

	void ReloadSkinDetail();
	void LoadSkinDetailTexture();

	struct Settings
	{
		bool EnableSkin = true;
		float SkinMainRoughness = 0.7f;
		float SkinSecondRoughness = 0.35f;
		float SkinSpecularTexMultiplier = 1.0f;
		float SecondarySpecularStrength = 0.15f;
		float F0 = 0.0278f;
		float BaseColorMultiplier = 1.0f;
		float PhysicalMainRoughnessMultiplier = 1.3f;
		float PhysicalSecondRoughnessMultiplier = 0.75f;
		float PhysicalSpecularStrength = 1.0f;
		float ExtraEdgeRoughness = 0.25f;
		bool EnableSkinDetail = true;
		float SkinDetailStrength = 0.25f;
		float SkinDetailTiling = 10.0f;
		float BodyTilingMultiplier = 2.0f;
		float ExtraSkinWetness = 0.0f;
		float WetFadeTime = 10.0f;
		float StartSweat = 0.75f;
		float FullSweat = 0.15f;
		float4 WetParams = { 512.0f, 0.7f, 10.0f, 4.0f };
		float Translucency = 0.1f;
		float sssWidth = 0.2f;
		bool UseSSS = true;
		float FuzzStrength = 1.0f;
		float FuzzRoughness = 0.35f;
		float FuzzF0 = 0.045f;
		bool UseDynamicWetness = false;
	} settings;

	struct alignas(16) SkinData
	{
		float4 skinParams;
		float4 skinParams2;
		float4 skinDetailParams;
		float4 sssParams;
		float4 fuzzParams;
		float4 physicalParams;
		float4 wetParams;
	};

	struct alignas(16) PerGeometryData
	{
		float4 skinPerGeometry;
	};

	eastl::unique_ptr<ConstantBuffer> PerGeometryCB;
	float4 currentWetness = { 0.0f, 0.0f, 0.0f, 0.0f };
	float playerStamina = 0.0f;
	float playerStaminaMax = 0.0f;

	struct WaterHeightCacheEntry
	{
		uint frameCount = 0;
		float waterHeight = 0.0f;
	};
	std::unordered_map<uint32_t, WaterHeightCacheEntry> waterHeightCache;  // keyed by actor formID

	struct ExtraTextures
	{
		RE::NiSourceTexturePtr rfaosTexture;
		RE::NiSourceTexturePtr wetnessTexture;
		std::string extraTexturePath;
		std::string wetnessTexturePath;
		bool hasExtraTexture = false;
		bool hasWetnessTexture = false;
	};

	eastl::unique_ptr<Texture2D> texSkinDetail = nullptr;
	std::unordered_map<uint32_t, ExtraTextures> skinExtraTextures;
	std::unordered_map<uint32_t, float4> actorWetnessMap;  // keyed by actor formID

	SkinData GetCommonBufferData();
	float GetWaterHeight(const RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos);
	float4 GetWetness(RE::BSGeometry* geometry);

	void SetupExtraTexture(RE::BSLightingShaderMaterialBase const* material, RE::BSTextureSet* inTextureSet, uint32_t i_hashKey);
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);
	void BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass);
	void SetShaderResources(ID3D11DeviceContext* a_context);

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
			logger::info("[Advanced Skin] Installed hooks");
			return;
		}
	};

	bool isDynamicWetnessAvailable = false;
};
