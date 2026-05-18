#pragma once

#include "Buffer.h"

#define SSSS_N_SAMPLES 21

struct SubsurfaceScattering : Feature
{
public:
	struct DiffusionProfile
	{
		float BlurRadius;
		float Thickness;
		float3 Strength;
		float3 Falloff;
	};

	enum ScatterMode : int
	{
		kPreScatter = 0,
		kPostScatter = 1,
		kPreAndPostScatter = 2,
	};

	struct Settings
	{
		uint EnableCharacterLighting = false;
		float CharacterLightingStrength = 1.0f;
		int SSMode = 0;
		int ScatterMode = kPreAndPostScatter;
		DiffusionProfile BaseProfile{ 1.0f, 1.0f, { 0.48f, 0.41f, 0.28f }, { 0.56f, 0.56f, 0.56f } };
		DiffusionProfile HumanProfile{ 1.0f, 1.0f, { 0.48f, 0.41f, 0.28f }, { 1.0f, 0.37f, 0.3f } };
		uint BurleySamples = 16;
		float4 MeanFreePathBase = { 0.56f, 0.56f, 0.56f, 2.67f };
		float4 MeanFreePathHuman = { 1.0f, 0.37f, 0.3f, 2.67f };
	};

	Settings settings;

	float CharacterLightingStrengthOriginal = -1.0f;

	struct alignas(16) Kernel
	{
		float4 Sample[SSSS_N_SAMPLES];
	};
	STATIC_ASSERT_ALIGNAS_16(Kernel);

	struct alignas(16) BlurCB
	{
		Kernel BaseKernel;
		Kernel HumanKernel;
		float4 BaseProfile;
		float4 HumanProfile;
		float SSSS_FOVY;
		uint BurleySamples;
		uint ScatterMode;
		uint pad;
		float4 MeanFreePathBase;
		float4 MeanFreePathHuman;
	};
	STATIC_ASSERT_ALIGNAS_16(BlurCB);

	ConstantBuffer* blurCB = nullptr;
	BlurCB blurCBData{};

	bool validMaterial = true;
	bool updateKernels = true;
	bool validMaterials = false;

	Texture2D* blurHorizontalTemp = nullptr;
	Texture2D* diffuseNoAlbedoTex = nullptr;

	ID3D11ComputeShader* prepassSS = nullptr;
	ID3D11ComputeShader* horizontalSSBlur = nullptr;
	ID3D11ComputeShader* verticalSSBlur = nullptr;
	ID3D11ComputeShader* burleySS = nullptr;
	RE::BGSKeyword* isBeastRaceKeyword = nullptr;

	virtual inline std::string GetName() override { return "Subsurface Scattering"; }
	virtual inline std::string GetShortName() override { return "SubsurfaceScattering"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SSS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kCharacters; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Subsurface Scattering simulates light penetration through translucent materials like skin, creating more realistic character lighting.\n"
			"This technique makes organic materials appear more lifelike and natural.",
			{ "Realistic skin lighting",
				"Light penetration simulation",
				"Separate profiles for different materials",
				"Enhanced character appearance",
				"Configurable scattering properties" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void RestoreDefaultSettings() override;

	virtual void DrawSettings() override;

	float3 Gaussian(DiffusionProfile& a_profile, float variance, float r);
	float3 Profile(DiffusionProfile& a_profile, float r);
	void CalculateKernel(DiffusionProfile& a_profile, Kernel& kernel);

	void DrawSSS();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void ClearShaderCache() override;
	ID3D11ComputeShader* GetComputeShaderPrepass();
	ID3D11ComputeShader* GetComputeShaderHorizontalBlur();
	ID3D11ComputeShader* GetComputeShaderVerticalBlur();
	ID3D11ComputeShader* GetComputeShaderBurley();

	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;

	void BSLightingShader_SetupSkin(RE::BSRenderPass* Pass);

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
			logger::info("[SSS] Installed hooks");
		}
	};

	virtual bool SupportsVR() override { return true; };
};
