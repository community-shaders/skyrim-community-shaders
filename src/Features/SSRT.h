#pragma once

#include "Buffer.h"
#include "NRDReblurIntegration.h"
#include <NRDSettings.h>

struct SSRT : Feature
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	bool inline SupportsVR() override { return false; }

	virtual inline std::string GetName() override { return "SSRT"; }
	virtual inline std::string GetShortName() override { return "SSRT"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return std::make_pair(
			std::string(
				"Screen Space Ray Tracing (SSRT3) adds realistic indirect lighting and "
				"ambient occlusion using horizon-based sampling with bitfield visibility "
				"tracking. Ported from SSRT3 by CDRIN (Olivier Therrien)."),
			std::vector<std::string>{
				"Realistic indirect lighting",
				"Enhanced ambient occlusion",
				"Horizon-based sampling with 32-bit visibility",
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

	void DrawSSRT();
	void UpdateCB();

	//////////////////////////////////////////////////////////////////////////////////

	bool recompileFlag = false;

	struct Settings
	{
		bool Enabled = true;
		bool EnableVanillaSSAO = false;
		// Sampling
		uint RotationCount = 1;
		uint StepCount = 12;
		float Radius = 256.f;
		float ExpFactor = 2.f;
		bool JitterSamples = true;
		bool ScreenSpaceSampling = false;
		bool MipOptimization = true;
		// GI
		float GIIntensity = 10.f;
		bool NormalApproximation = false;
		// Occlusion
		float AOIntensity = 1.f;
		float Thickness = 1.f;
		bool LinearThickness = false;
		// NRD REBLUR (performance-tuned defaults)
		bool EnableNRD = true;
		float HitDistA = 210.f;
		float HitDistB = 0.1f;
		float HitDistC = 20.f;
		float HitDistD = -25.f;
		uint MaxAccumulatedFrameNum = 20;
		uint MaxFastAccumulatedFrameNum = 4;
		uint MaxStabilizedFrameNum = 0;
		uint HistoryFixFrameNum = 0;
		float DiffusePrepassBlurRadius = 0.f;
		float MinBlurRadius = 0.5f;
		float MaxBlurRadius = 15.f;
		bool EnableAntiFirefly = false;
	} settings;

	struct alignas(16) SSRTCB
	{
		float4 NDCToViewMul;
		float4 NDCToViewAdd;

		float2 TexDim;
		float2 RcpTexDim;
		float2 FrameDim;
		float2 RcpFrameDim;

		uint FrameIndex;
		uint RotationCount;
		uint StepCount;
		float Radius;

		float ExpFactor;
		float HalfProjScale;
		uint JitterSamples;
		uint ScreenSpaceSampling;

		uint MipOptimization;
		float GIIntensity;
		float AOIntensity;
		float Thickness;

		uint LinearThickness;
		float TemporalOffsets;
		float TemporalDirections;
		float pad0;

		float HitDistA;
		float HitDistB;
		float HitDistC;
		float HitDistD;
	};
	STATIC_ASSERT_ALIGNAS_16(SSRTCB);
	eastl::unique_ptr<ConstantBuffer> ssrtCB;

	eastl::unique_ptr<Texture2D> texGIOcclusion = nullptr;

	eastl::unique_ptr<Texture2D> texWorkingDepth = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavWorkingDepth[5] = { nullptr };

	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavRadiance[5] = { nullptr };

	eastl::unique_ptr<Texture2D> texNormals = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavNormals[5] = { nullptr };

	// NRD textures
	eastl::unique_ptr<Texture2D> texNRDViewZ = nullptr;
	eastl::unique_ptr<Texture2D> texNRDNormalRoughness = nullptr;
	eastl::unique_ptr<Texture2D> texNRDInputDiffuse = nullptr;
	eastl::unique_ptr<Texture2D> texNRDOutputDiffuse = nullptr;

	NRDReblurIntegration nrdReblur;
	nrd::ReblurSettings reblurSettings{};
	uint32_t nrdFrameIndex = 0;
	Matrix prevViewMatrix{};
	Matrix prevProjMatrix{};

	inline ID3D11ShaderResourceView* GetOutputTexture()
	{
		if (!loaded || !settings.Enabled)
			return nullptr;
		return texGIOcclusion->srv.get();
	}

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterRadianceCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterNormalsCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> ssrtCSCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prepareNRDGuidesCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> resolveNRDCompute = nullptr;
};
