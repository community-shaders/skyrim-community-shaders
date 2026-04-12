#pragma once

#include "Buffer.h"

struct SSRT : Feature
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	bool inline SupportsVR() override { return true; }

	virtual inline std::string GetName() override { return "SSRT"; }
	virtual inline std::string GetShortName() override { return "SSRT"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		std::string desc =
			"Screen Space Ray Tracing (SSRT3) adds realistic indirect lighting and "
			"ambient occlusion using horizon-based sampling with bitfield visibility "
			"tracking. Ported from SSRT3 by CDRIN (Olivier Therrien).";
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
		float Radius = 5.f;
		float ExpFactor = 1.f;
		bool JitterSamples = true;
		bool ScreenSpaceSampling = false;
		bool MipOptimization = true;
		// GI
		float GIIntensity = 10.f;
		bool NormalApproximation = false;
		float BackfaceLighting = 0.f;
		// Occlusion
		float AOIntensity = 1.f;
		float Thickness = 1.f;
		bool LinearThickness = false;
		// Fallback
		bool EnableFallback = true;
		uint FallbackSampleCount = 4;
		float FallbackIntensity = 1.f;
		float FallbackPower = 1.f;
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
		float BackfaceLighting;
		float AOIntensity;

		float Thickness;
		uint LinearThickness;
		uint FallbackSampleCount;
		float FallbackIntensity;

		float FallbackPower;
		float TemporalOffsets;
		float TemporalDirections;
		float pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(SSRTCB);
	eastl::unique_ptr<ConstantBuffer> ssrtCB;

	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	eastl::unique_ptr<Texture2D> texGIOcclusion = nullptr;

	inline ID3D11ShaderResourceView* GetOutputTexture()
	{
		return (loaded && settings.Enabled) ?
		           texGIOcclusion->srv.get() :
		           (ID3D11ShaderResourceView*)nullptr;
	}

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> ssrtCSCompute = nullptr;
};
