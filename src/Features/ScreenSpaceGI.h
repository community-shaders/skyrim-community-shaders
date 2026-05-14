#pragma once

#include "Buffer.h"
#include "NRDReblurIntegration.h"

#include <NRDSettings.h>

struct ScreenSpaceGI : Feature
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	bool inline SupportsVR() override { return true; }

	virtual inline std::string GetName() override { return "Screen Space GI"; }
	virtual inline std::string GetShortName() override { return "ScreenSpaceGI"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		std::string desc =
			"Screen Space Global Illumination adds realistic indirect lighting and "
			"ambient occlusion to the game. This technique simulates how light "
			"bounces off surfaces to illuminate other objects naturally.";
		if (REL::Module::IsVR()) {
			desc +=
				"\n\nWarning: In VR, this feature may have visual artifacts and "
				"can have a significant performance impact due to the nature of "
				"screen space effects.";
		}
		return std::make_pair(
			desc,
			std::vector<std::string>{
				"Realistic indirect lighting",
				"Enhanced ambient occlusion",
				"Improved visual depth and atmosphere",
				"NVIDIA REBLUR temporal denoising",
				"Configurable quality and performance settings" });
	}

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();
	bool ShadersOK();

	void DrawSSGI();
	void UpdateSB();

	//////////////////////////////////////////////////////////////////////////////////

	bool recompileFlag = false;

	struct REBLURSettings
	{
		uint32_t MaxAccumulatedFrameNum = 30;
		uint32_t MaxFastAccumulatedFrameNum = 6;
		uint32_t MaxStabilizedFrameNum = 5;
		uint32_t HistoryFixFrameNum = 3;
		uint32_t HistoryFixBasePixelStride = 14;
		uint32_t HistoryFixAlternatePixelStride = 1;
		float FastHistoryClampingSigmaScale = 2.0f;
		float MinHitDistanceWeight = 0.1f;
		float MinBlurRadius = 1.0f;
		float MaxBlurRadius = 35.0f;
		float LobeAngleFraction = 0.5f;
		float RoughnessFraction = 0.15f;
		float PlaneDistanceSensitivity = 0.005f;
		float SplitScreen = 0.0f;
		uint32_t HitDistanceReconstructionMode = 0;
	};

	struct Settings
	{
		bool Enabled = true;
		bool EnableGI = REL::Module::IsVR() ? false : true;
		bool EnableVanillaSSAO = false;
		// performance/quality
		uint NumSteps = 32u;
		// visual
		float MinScreenRadius = 0.01f;
		float Radius = 512.f;
		float Thickness = 32.f;
		float2 DepthFadeRange = { 4e4, 5e4 };
		// mix
		float AOPower = 1.0f;
		float GIStrength = 1.0f;
		// NRD REBLUR
		bool EnableREBLUR = true;
		REBLURSettings Reblur;
	} settings;

	struct alignas(16) SSGICB
	{
		float4x4 PrevInvViewMat[2];
		float2 NDCToViewMul[2];
		float2 NDCToViewAdd[2];

		float2 TexDim;
		float2 RcpTexDim;
		float2 FrameDim;
		float2 RcpFrameDim;
		uint FrameIndex;

		uint NumSteps;

		float MinScreenRadius;
		float Radius;
		float Thickness;
		float2 DepthFadeRange;
		float DepthFadeScaleConst;

		float AOPower;
		float GIStrength;
		float pad1[2];
	};
	STATIC_ASSERT_ALIGNAS_16(SSGICB);
	eastl::unique_ptr<ConstantBuffer> ssgiCB;

	struct alignas(16) SharedData
	{
		float DiffuseMult;
		uint DebugMode;
		float2 _pad;
	};

	SharedData GetCommonBufferData();

	eastl::unique_ptr<Texture2D> texNoise = nullptr;
	eastl::unique_ptr<Texture2D> texWorkingDepth = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavWorkingDepth[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texPrevGeo = nullptr;
	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavRadiance[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texNormal = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavNormal[5] = { nullptr };

	// NRD textures
	eastl::unique_ptr<Texture2D> texNRDInput = nullptr;
	eastl::unique_ptr<Texture2D> texNRDOutput = nullptr;
	eastl::unique_ptr<Texture2D> texNRDMV = nullptr;
	eastl::unique_ptr<Texture2D> texNRDViewZ = nullptr;
	eastl::unique_ptr<Texture2D> texNRDNormalRoughness = nullptr;

	ID3D11ShaderResourceView* GetDiffuseOutputTexture();

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterRadianceCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterNormalCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> giCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> stereoSyncCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prepareNRDGuidesCompute = nullptr;

	NRDReblurIntegration nrdReblur;
	nrd::ReblurSettings reblurSettings{};
	uint32_t frameIndex = 0;

	Matrix worldToViewMat{};
	Matrix prevWorldToViewMat{};
	Matrix prevProjMatrix{};
	float2 prevJitter{};
};
