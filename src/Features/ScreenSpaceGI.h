#pragma once

struct ScreenSpaceGI : Feature
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	bool inline SupportsVR() override { return false; }

	virtual inline std::string GetName() override { return "Screen Space GI"; }
	virtual inline std::string GetShortName() override { return "ScreenSpaceGI"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "SSGI"; }
	virtual std::string_view GetCategory() const override { return "Lighting"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
	{
		return t == RE::BSShader::Type::Lighting ||
		       t == RE::BSShader::Type::Grass ||
		       t == RE::BSShader::Type::DistantTree;
	};

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
				"Temporal denoising for smooth results",
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
	uint outputAoIdx = 0;
	uint outputIlIdx = 0;

	struct Settings
	{
		bool Enabled = REL::Module::IsVR() ? false : true;   // disabled in VR by default
		bool EnableGI = REL::Module::IsVR() ? false : true;  // AO only for VR by default
		bool EnableExperimentalSpecularGI = false;
		// performance/quality
		uint NumSlices = REL::Module::IsVR() ? 1u : 4u;  // AO preset for VR
		uint NumSteps = REL::Module::IsVR() ? 6u : 8u;   // AO preset for VR
		int ResolutionMode = 1;                          // 0-full, 1-half, 2-quarter - DBF default
		// visual
		float MinScreenRadius = 0.01f;
		float AORadius = 256.f;
		float GIRadius = 256.f;
		float Thickness = 32.f;
		float2 DepthFadeRange = { 4e4, 5e4 };
		// gi
		float GISaturation = 0.9f;
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
		float4x4 PrevInvViewMat[2];
		float2 NDCToViewMul[2];
		float2 NDCToViewAdd[2];

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
	eastl::unique_ptr<ConstantBuffer> ssgiCB;

	eastl::unique_ptr<Texture2D> texNoise = nullptr;
	eastl::unique_ptr<Texture2D> texWorkingDepth = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavWorkingDepth[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texPrevGeo = nullptr;
	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	eastl::unique_ptr<Texture2D> texRadianceTemp = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavRadiance[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texAccumFrames[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texAo[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texIlY[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texIlCoCg[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texGiSpecular[2] = { nullptr };

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
	winrt::com_ptr<ID3D11ComputeShader> radianceDisoccCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> giCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> upsampleCompute = nullptr;
};