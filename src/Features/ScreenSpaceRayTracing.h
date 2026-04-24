#pragma once

#include "NRDReblurIntegration.h"
#include <NRDSettings.h>

struct ScreenSpaceRayTracing : Feature
{
	static ScreenSpaceRayTracing* GetSingleton()
	{
		static ScreenSpaceRayTracing singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Screen Space Ray Tracing"; }
	virtual inline std::string GetShortName() override { return "ScreenSpaceRayTracing"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SSRT"; }
	virtual std::string_view GetCategory() const override { return "Lighting"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Screen Space Ray Tracing provides high-quality global illumination with information in screen space.",
			{ "Realistic indirect lighting",
				"Importance sampling for advanced reflections based on roughness",
				"Efficient ray marching with Hi-Z buffer",
				"Uses dynamic cubemaps as fallback for missing information",
				"NVIDIA REBLUR temporal denoising for diffuse GI" }
		};
	}

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };
	virtual bool SupportsVR() override { return true; };

	struct REBLURSettings
	{
		uint32_t MaxAccumulatedFrameNum = 30;
		uint32_t MaxFastAccumulatedFrameNum = 6;
		uint32_t MaxStabilizedFrameNum = 5;
		uint32_t HistoryFixFrameNum = 3;
		uint32_t HistoryFixBasePixelStride = 14;
		uint32_t HistoryFixAlternatePixelStride = 1;
		float FastHistoryClampingSigmaScale = 2.0f;
		float DiffusePrepassBlurRadius = 30.0f;
		float MinHitDistanceWeight = 0.1f;
		float MinBlurRadius = 1.0f;
		float MaxBlurRadius = 35.0f;
		float LobeAngleFraction = 0.5f;
		float RoughnessFraction = 0.15f;
		float PlaneDistanceSensitivity = 0.005f;
		float FireflySuppressorMinRelativeScale = 1.5f;
		bool EnableAntiFirefly = false;
		bool ReturnHistoryLengthInsteadOfOcclusion = false;
		float SplitScreen = 0.0f;
		// 0 = OFF (default), 1 = AREA_3X3 (performance mode — sparser raycast OK), 2 = AREA_5X5
		uint32_t HitDistanceReconstructionMode = 0;
		bool EnableNRDValidation = false;  // NRD internal debug overlay written to output textures
	};

	// FULL: every pixel traces both diffuse and specular (2 dispatches, 2 rays/pixel).
	// FULL_PROBABILISTIC: every pixel traces ONE ray, randomly diffuse vs specular — requires NRD AREA_3X3 reconstruction.
	// HALF: checkerboard — even pixels diffuse, odd pixels specular. ~half the ray cost.
	// QUARTER: half checkerboard + row alternation — 1/4 pixels traced per frame. NRD reconstructs columns; temporal fills rows.
	enum TracingMode : uint
	{
		TRACING_MODE_FULL = 0,
		TRACING_MODE_FULL_PROBABILISTIC = 1,
		TRACING_MODE_HALF = 2,
		TRACING_MODE_QUARTER = 3,
	};

	struct Settings
	{
		bool EnableSpecular = true;
		uint Mode = TRACING_MODE_HALF;
		uint MaxSteps = 128;
		uint MaxMips = kMaxMipSlots;
		float Thickness = 5.f;
		float NormalBias = 0.1f;
		float BRDFBias = 0.25f;
		bool UseDynamicCubemapsAsFallback = true;
		bool UseDynamicCubemapsAsFallbackSpecular = true;
		bool EnableDiffuse = true;
		float SpecularMult = 1.0f;
		float DiffuseMult = 1.0f;
		float AmbientMult = 0.0f;
		float OcclusionStrength = 1.0f;
		bool EnableREBLUR = true;
		float HitDistA = 210.0f;  // NRD hitDistanceParameters.A (typical hit dist, Skyrim units)
		float HitDistB = 0.1f;    // NRD hitDistanceParameters.B (correction factor)
		float HitDistC = 20.0f;   // NRD hitDistanceParameters.C (sky hit dist)
		float HitDistD = -25.0f;  // NRD hitDistanceParameters.D (thin surface correction)
		uint DebugMode = 0;       // 0=none,1=spec,2=diffuse,3=occlusion,4=depth
		bool EnablePrevGIReprojection = true;
		REBLURSettings Reblur;
	} settings;

	struct alignas(16) SharedData
	{
		uint EnableSpecular;
		float SpecularMult;
		float DiffuseMult;
		float AmbientMult;
		uint DebugMode;
		uint EnablePrevGIReprojection;
		float _pad0[2];  // pad to 32 bytes to match HLSL SSRTSettings cbuffer layout
	};

	struct alignas(16) SSRTCB
	{
		uint MaxSteps;
		uint MaxMips;
		uint UseDynamicCubemapsAsFallback;
		float Thickness;
		float NormalBias;
		float BRDFBias;
		float OcclusionStrength;
		float _pad0;

		float2 TexDim;
		float2 RcpTexDim;
		float2 FrameDim;
		float2 RcpFrameDim;
		float HitDistA;
		float HitDistB;
		float HitDistC;
		float HitDistD;
		uint FrameIndex;
		uint TracingMode;
		uint _pad1[2];
	};

	eastl::unique_ptr<ConstantBuffer> ssrtCB;

	bool recompileFlag = false;

	void DrawSSRTSpecular();
	void DrawSSRTDiffuse();
	virtual void Prepass() override;

	SharedData GetCommonBufferData();

	// Returns SRVs for the two NRD-packed SH output textures (or input if REBLUR disabled).
	// sh[0] = SH0 (Y/DC + normHitDist), sh[1] = SH1 (directional)
	struct DiffuseOutput
	{
		ID3D11ShaderResourceView* sh[2];
	};
	DiffuseOutput GetDiffuseOutputTextures();
	ID3D11ShaderResourceView* GetSpecularOutputTexture();

	// Ray march intermediate textures
	eastl::unique_ptr<Texture2D> texDepth = nullptr;  // Hi-Z pyramid base (1/2 res)

	// NRD specular textures (checkerboard packed, full-res allocation, RGBA16F)
	eastl::unique_ptr<Texture2D> texNRDInputSpecRadianceHitDist = nullptr;   // IN_SPEC_RADIANCE_HITDIST
	eastl::unique_ptr<Texture2D> texNRDOutputSpecRadianceHitDist = nullptr;  // OUT_SPEC_RADIANCE_HITDIST

	// NRD SH textures (checkerboard packed, full-res allocation, RGBA16F)
	eastl::unique_ptr<Texture2D> texNRDInputSH0 = nullptr;   // IN_DIFF_SH0
	eastl::unique_ptr<Texture2D> texNRDInputSH1 = nullptr;   // IN_DIFF_SH1
	eastl::unique_ptr<Texture2D> texNRDOutputSH0 = nullptr;  // OUT_DIFF_SH0
	eastl::unique_ptr<Texture2D> texNRDOutputSH1 = nullptr;  // OUT_DIFF_SH1

	// NRD guide textures (full-res)
	eastl::unique_ptr<Texture2D> texNRDViewZ = nullptr;            // IN_VIEWZ (R32F)
	eastl::unique_ptr<Texture2D> texNRDNormalRoughness = nullptr;  // IN_NORMAL_ROUGHNESS (R8G8B8A8)

	winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV = nullptr;

	static const uint kMaxMipSlots = 12;
	uint numMips = 1;

	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kMaxMipSlots> depthSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMaxMipSlots> depthUAVs = { nullptr };

	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> raymarchSpecularCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> raymarchDiffuseCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> depthDownsampleCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prepareNRDGuidesCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> applyGIToMainCS = nullptr;

	NRDReblurIntegration nrdReblur;
	nrd::ReblurSettings reblurSettings{};
	NRDReblurIntegration nrdReblurSpecular;
	nrd::ReblurSettings reblurSpecularSettings{};
	uint32_t frameIndex = 0;

	Matrix prevViewMatrix{};
	Matrix prevProjMatrix{};
};
