#pragma once

#define SSSS_N_SAMPLES 21

struct SubsurfaceScattering : Feature
{
public:
	static SubsurfaceScattering* GetSingleton()
	{
		static SubsurfaceScattering singleton;
		return &singleton;
	}

	struct DiffusionProfile
	{
		float BlurRadius;
		float Thickness;
		float3 Strength;
		float3 Falloff;
	};

	struct Settings
	{
		uint EnableCharacterLighting = false;
		DiffusionProfile BaseProfile{ 0.25f, 1.0f, { 0.48f, 0.41f, 0.28f }, { 0.56f, 0.56f, 0.56f } };
		DiffusionProfile HumanProfile{ 0.5f, 1.0f, { 0.48f, 0.41f, 0.28f }, { 1.0f, 0.37f, 0.3f } };
	};

	Settings settings;

	struct alignas(16) Kernel
	{
		float4 Sample[SSSS_N_SAMPLES];
	};

	struct alignas(16) BlurCB
	{
		Kernel BaseKernel;
		Kernel HumanKernel;
		float4 BaseProfile;
		float4 HumanProfile;
		float SSSS_FOVY;
		uint pad[3];
	};

	ConstantBuffer* blurCB = nullptr;
	BlurCB blurCBData{};

	bool validMaterial = true;
	bool updateKernels = true;
	bool validMaterials = false;

	Texture2D* blurHorizontalTemp = nullptr;

	ID3D11ComputeShader* horizontalSSBlur = nullptr;
	ID3D11ComputeShader* verticalSSBlur = nullptr;
	RE::BGSKeyword* isBeastRaceKeyword = nullptr;

	virtual inline std::string GetName() override { return "Subsurface Scattering"; }
	virtual inline std::string GetShortName() override { return "SubsurfaceScattering"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SSS"; }

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
	ID3D11ComputeShader* GetComputeShaderHorizontalBlur();
	ID3D11ComputeShader* GetComputeShaderVerticalBlur();

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
