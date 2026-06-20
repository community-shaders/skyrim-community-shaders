#pragma once

#include "Buffer.h"

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
		return { desc,
			{ T("feature.screen_space_gi.key_feature_1", "Realistic indirect lighting"),
				T("feature.screen_space_gi.key_feature_2", "Enhanced ambient occlusion"),
				T("feature.screen_space_gi.key_feature_3", "Improved visual depth and atmosphere"),
				T("feature.screen_space_gi.key_feature_4", "Temporal denoising for smooth results"),
				T("feature.screen_space_gi.key_feature_5", "Configurable quality and performance settings") } };
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

	/** @brief Executes the full SSGI pipeline: depth prefilter, radiance fetch, GI, blur, and upsample. */
	void DrawSSGI();
	/** @brief Updates the SSGI constant buffer with current camera, resolution, and settings data. */
	void UpdateSB();

	//////////////////////////////////////////////////////////////////////////////////

	bool recompileFlag = false;
	uint outputAoIdx = 0;
	uint outputIlIdx = 0;

	struct Settings
	{
		bool Enabled = true;
		bool EnableGI = true;
		bool EnableExperimentalSpecularGI = false;
		bool EnableVanillaSSAO = false;
		// performance/quality
		uint NumSlices = 4u;
		uint NumSteps = 8u;
		int ResolutionMode = 1;  // 0-full, 1-half, 2-quarter - DBF default
		// visual
		float MinScreenRadius = 0.01f;
		float AORadius = 256.f;
		float GIRadius = 256.f;
		float Thickness = 32.f;
		float2 DepthFadeRange = { 4e4, 5e4 };
		// gi
		float GISaturation = 0.8f;
		float GIDistanceCompensation = 0.f;
		// mix
		float AOPower = 1.0f;
		float GIStrength = 1.0f;
		// denoise
		bool EnableTemporalDenoiser = true;
		bool EnableBlur = true;
		float DepthDisocclusion = .1f;
		float NormalDisocclusion = .1f;
		uint MaxAccumFrames = 16;
		float BlurRadius = 2.f;
		float DistanceNormalisation = 2.f;
	} settings;

	struct alignas(16) SSGICB
	{
		float4x4 PrevInvViewMat;
		float4 NDCToViewMul;
		float4 NDCToViewAdd;

		float2 TexDim;
		float2 RcpTexDim;  //
		float2 FrameDim;
		float2 RcpFrameDim;  //
		uint FrameIndex;

		uint NumSlices;
		uint NumSteps;

		float MinScreenRadius;  //
		float AORadius;
		float GIRadius;
		float EffectRadius;
		float Thickness;  //
		float2 DepthFadeRange;
		float DepthFadeScaleConst;

		float GISaturation;  //
		float GIDistanceCompensation;
		float GICompensationMaxDist;
		float pad1;

		float AOPower;  //
		float GIStrength;

		float DepthDisocclusion;
		float NormalDisocclusion;
		uint MaxAccumFrames;  //

		float BlurRadius;
		float DistanceNormalisation;

		float2 pad;
	};
	STATIC_ASSERT_ALIGNAS_16(SSGICB);
	eastl::unique_ptr<ConstantBuffer> ssgiCB;

	eastl::unique_ptr<Texture2D> texNoise = nullptr;
	eastl::unique_ptr<Texture2D> texWorkingDepth = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavWorkingDepth[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texPrevGeo = nullptr;
	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	eastl::unique_ptr<Texture2D> texRadianceTemp = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavRadiance[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texNormal = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavNormal[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texAccumFrames[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texAo[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texIlY[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texIlCoCg[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texGiSpecular[2] = { nullptr };

	/** @brief Returns the current output SRVs for AO, indirect lighting Y/CoCg, and specular GI (or nullptrs if disabled). */
	inline auto GetOutputTextures()
	{
		return (loaded && settings.Enabled) ?
		           std::make_tuple(
					   texAo[outputAoIdx]->srv.get(),
					   texIlY[outputIlIdx]->srv.get(),
					   texIlCoCg[outputIlIdx]->srv.get(),
					   texGiSpecular[outputAoIdx]->srv.get()) :
		           std::make_tuple(nullptr, nullptr, nullptr, nullptr);
	}

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterRadianceCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterNormalCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> radianceDisoccCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> giCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> upsampleCompute = nullptr;
};
