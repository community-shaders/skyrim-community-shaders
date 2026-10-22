#pragma once

#include "Buffer.h"
#include "NRD.h"
#include "NRDReblurIntegration.h"

#include <NRDSettings.h>

/**
 * ScreenSpaceGI: indirect lighting and ambient occlusion via XeGTAO-style
 * horizon scanning, optionally directional via spherical harmonics, denoised
 * by NVIDIA REBLUR.
 *
 * Diffuse-only. Specular reflections live in the standalone
 * ScreenSpaceReflections feature; denoising guides come from the core NRD
 * service.
 */
struct ScreenSpaceGI : Feature
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	virtual inline std::string GetName() override { return "Screen Space GI"; }
	virtual std::string GetDisplayName() override { return T("feature.screen_space_gi.name", "Screen Space GI"); }
	virtual inline std::string GetShortName() override { return "ScreenSpaceGI"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		std::string desc = T("feature.screen_space_gi.description", "Screen Space Global Illumination adds realistic indirect lighting and ambient occlusion to the game. This technique simulates how light bounces off surfaces to illuminate other objects naturally.");
		return std::make_pair(
			desc,
			std::vector<std::string>{
				T("feature.screen_space_gi.key_feature_1", "Realistic indirect lighting"),
				T("feature.screen_space_gi.key_feature_2", "Enhanced ambient occlusion"),
				T("feature.screen_space_gi.key_feature_3", "Improved visual depth and atmosphere"),
				T("feature.screen_space_gi.key_feature_4", "NVIDIA REBLUR temporal denoising"),
				T("feature.screen_space_gi.key_feature_5", "Configurable quality and performance settings") });
	}

	/** @brief Resets all settings to their default values and flags shaders for recompilation. */
	virtual void RestoreDefaultSettings() override;
	/** @brief Draws the ImGui settings UI with quality presets, visual parameters, and denoising options. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Creates GPU textures, samplers, constant buffers, and compiles compute shaders. */
	virtual void SetupResources() override;
	/** @brief Releases and recompiles all SSGI compute shaders. */
	virtual void ClearShaderCache() override;
	/** @brief Compiles all SSGI compute shaders with current resolution and feature defines. */
	void CompileComputeShaders();
	/** @brief Checks whether all required compute shaders and the noise texture loaded successfully. */
	bool ShadersOK();

	/** @brief Executes the full SSGI pipeline: depth prefilter, radiance fetch, GI, and denoising. */
	void DrawSSGI();
	/** @brief Composites this frame's SSGI indirect diffuse lighting into the main render target before downstream screen-space passes. */
	void Composite();
	/** @brief Updates the SSGI constant buffer with current camera, resolution, and settings data. */
	void UpdateSB();
	void SetupNRDResources();

	//////////////////////////////////////////////////////////////////////////////////

	bool recompileFlag = false;

	struct Settings
	{
		bool Enabled = true;
		bool EnableGI = true;
		bool EnableVanillaSSAO = false;
		bool EnableSH = true;
		// performance/quality
		uint NumSteps = 8u;
		bool HalfRes = true;
		bool QuarterRes = false;
		// visual
		float Thickness = 0.1f;
		// mix
		float AOPower = 1.0f;
		float GIStrength = 1.0f;
		bool EnableMultiBounce = false;
		bool UseDynamicCubemapsAsFallback = true;
		float DiffuseCubemapMult = 1.0f;
		// NRD REBLUR
		bool EnableREBLUR = true;
		NRD::REBLURSettings Reblur;
	} settings;

	struct alignas(16) SSGICB
	{
		float2 NDCToViewMul;
		float2 NDCToViewAdd;

		float2 TexDim;
		float2 RcpTexDim;
		float2 FrameDim;
		float2 RcpFrameDim;
		uint FrameIndex;

		uint NumSteps;

		float Thickness;
		float AOPower;

		float GIStrength;
		float DiffuseCubemapMult;
		uint UseDynamicCubemap;
		uint MultiBounceMode;
	};
	STATIC_ASSERT_ALIGNAS_16(SSGICB);
	eastl::unique_ptr<ConstantBuffer> ssgiCB;

	struct alignas(16) SharedData
	{
		uint Enabled;
		uint EnableIL;
		uint DebugMode;
		float AOPower;
	};

	SharedData GetCommonBufferData();

	eastl::unique_ptr<Texture2D> texNoise = nullptr;
	eastl::unique_ptr<Texture2D> texWorkingDepth = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavWorkingDepth[5] = { nullptr };
	// Full-resolution geometry history is ping-ponged so radiance prefiltering can
	// read the previous frame while the normal pass writes the current frame.
	eastl::unique_ptr<Texture2D> texHistoryGeo[2] = { nullptr, nullptr };
	uint historyGeoWriteIndex = 0;
	bool hasMultiBounceHistory = false;
	bool hasFullResolutionMultiBounceHistory = false;
	bool multiBounceHistoryUsesNRDOutput = false;
	uint32_t lastMultiBounceHistoryFrame = 0;
	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavRadiance[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texNormal = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavNormal[5] = { nullptr };

	// REBLUR diffuse radiance in/out (RGBA16F, full-res)
	eastl::unique_ptr<Texture2D> texNRDInput = nullptr;
	eastl::unique_ptr<Texture2D> texNRDOutput = nullptr;
	eastl::unique_ptr<Texture2D> texNRDInputSH1 = nullptr;
	eastl::unique_ptr<Texture2D> texNRDOutputSH1 = nullptr;

	ID3D11ShaderResourceView* GetDiffuseOutputTexture();
	ID3D11ShaderResourceView* GetDiffuseSH1Texture();

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterRadianceCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterNormalCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> giCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compositeCompute = nullptr;

	NRDReblurIntegration nrdReblur;
	nrd::ReblurSettings reblurSettings{};
	bool resetReblurHistory = true;
	bool nrdReblurUsesSH = false;
	// Prevent downstream passes from sampling stale GI when this frame could not
	// produce a complete SSGI output (disabled feature, shader failure, etc.).
	bool outputReady = false;
};
