#pragma once

struct StochasticScreenSpaceReflections : Feature
{
	static StochasticScreenSpaceReflections* GetSingleton()
	{
		static StochasticScreenSpaceReflections singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Stochastic Screen Space Reflections"; }
	virtual inline std::string GetShortName() override { return "StochasticScreenSpaceReflections"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SSSR"; }
	virtual std::string_view GetCategory() const override { return "Lighting"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Stochastic Screen Space Reflections provides high-quality specular reflections using screen-space data.",
			{ "Glossy reflections driven by surface roughness",
				"Efficient ray marching with Hi-Z buffer",
				"Uses dynamic cubemaps as fallback for missing information",
				"Spatiotemporal Variance-Guided Filtering (SVGF) denoiser" }
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

	struct Settings
	{
		bool EnableSpecular = true;
		uint MaxSteps = 128;
		uint MaxMips = 6;
		float Thickness = 5.f;
		float NormalBias = 0.1f;
		float BRDFBias = 0.25f;
		bool UseDynamicCubemapsAsFallbackSpecular = true;
		float SpecularMult = 1.0f;
		float OcclusionStrength = 1.0f;
		float CubemapNormalization = 0.0f;
		bool EnableSVGF = false;
		uint MaxAccumulatedFrames = 16;
		uint AtrousIterations = 3;
		float ColorPhi = 0.5f;
		float NormalPhi = 512.0f;
	} settings;

	struct alignas(16) SharedData
	{
		uint EnableSpecular;
		float SpecularMult;
		float _padding[2];
	};

	struct alignas(16) SSSRCB
	{
		uint MaxSteps;
		uint MaxMips;
		uint UseDynamicCubemapsAsFallback;
		float Thickness;
		float NormalBias;
		float BRDFBias;
		float OcclusionStrength;
		float CubemapNormalization;
	};

	struct alignas(16) DenoiserCB
	{
		float invMaxAccumulatedFrames;
		uint atrousIterations;
		float colorPhi;
		float normalPhi;
	};

	eastl::unique_ptr<ConstantBuffer> sssrCB;
	eastl::unique_ptr<ConstantBuffer> denoiserCB;

	void DrawSSSRSpecular();
	virtual void Prepass() override;

	SharedData GetCommonBufferData();

	eastl::unique_ptr<Texture2D> texDepth = nullptr;
	eastl::unique_ptr<Texture2D> texColor = nullptr;
	eastl::unique_ptr<Texture2D> texSSRColor = nullptr;
	eastl::unique_ptr<Texture2D> texHitPDF = nullptr;
	eastl::unique_ptr<Texture2D> texHistory = nullptr;
	eastl::unique_ptr<Texture2D> texTemporal = nullptr;
	eastl::unique_ptr<Texture2D> texMoments = nullptr;
	eastl::unique_ptr<Texture2D> texHistoryMoments = nullptr;
	eastl::unique_ptr<Texture2D> texHistoryNormals = nullptr;
	eastl::unique_ptr<Texture2D> texVariance = nullptr;
	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV = nullptr;

	static const uint maxMips = 9;

	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, maxMips> depthSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, maxMips> depthUAVs = { nullptr };

	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> preprocessDepthCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> raymarchSpecularCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prepareColorCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> depthDownsampleCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> temporalCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> varianceCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spatialCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> spatialSpecularCS = nullptr;
};
