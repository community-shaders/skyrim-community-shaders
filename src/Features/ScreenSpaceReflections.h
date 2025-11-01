#pragma once

struct ScreenSpaceReflections : Feature
{
    static ScreenSpaceReflections* GetSingleton()
    {
        static ScreenSpaceReflections singleton;
        return &singleton;
    }

    virtual inline std::string GetName() override { return "Screen Space Reflections"; }
    virtual inline std::string GetShortName() override { return "ScreenSpaceReflections"; }
    virtual inline std::string_view GetShaderDefineName() override { return "SSR"; }
    virtual std::string_view GetCategory() const override { return "Lighting"; }
    virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Screen Space Reflections based on AMD FidelityFX Stochastic Screen Space Reflections (SSSR) implementation.",
            {
                "Realistic reflections on surfaces",
                "Importance sampling based on roughness",
                "HiZ depth buffer for efficient ray marching"
            }
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
        bool Enabled = true;
        uint MaxSteps = 128;
        uint MaxMips = 6;
        float Thickness = 5.f;
        float NormalBias = 0.25f;
        float BRDFBias = 0.25f;
        bool UseDynamicCubemapsAsFallback = true;
        uint DiffuseSPP = 2;
        bool EnableDiffuse = true;
        float SpecularMult = 1.0f;
        float DiffuseMult = 1.0f;
        float AmbientMult = 1.0f;
        float OcclusionStrength = 1.0f;
        bool EnableSVGF = false;
        uint MaxAccumulatedFrames = 16;
        uint AtrousIterations = 3;
        float ColorPhi = 0.5f;
        float NormalPhi = 128.0f;
        float DepthPhi = 4.0f;
#ifdef ENABLE_SHARC
        bool EnableSharc = false;
#endif
    } settings;

    struct alignas(16) SharedData
    {
        uint Enabled;
        float SpecularMult;
        float DiffuseMult;
        float AmbientMult;
    };

    struct alignas(16) SSRCB
    {
        uint MaxSteps;
        uint MaxMips;
        uint UseDynamicCubemapsAsFallback;
        float Thickness;
        float NormalBias;
        float BRDFBias;
        float OcclusionStrength;
        float pad;
    };

    struct alignas(16) DenoiserCB
    {
        float invMaxAccumulatedFrames;
        uint atrousIterations;
        float colorPhi;
        float normalPhi;
    };

    eastl::unique_ptr<ConstantBuffer> ssrCB;
    eastl::unique_ptr<ConstantBuffer> denoiserCB;

    bool recompileFlag = false;

    void DrawSSR();
    void DrawSSRTDiffuse();
    virtual void Prepass() override;

    SharedData GetCommonBufferData();

    eastl::unique_ptr<Texture2D> texDepth = nullptr;
    eastl::unique_ptr<Texture2D> texColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRTDiffuseColor = nullptr;
    eastl::unique_ptr<Texture2D> texHitPDF = nullptr;
    eastl::unique_ptr<Texture2D> texHistory = nullptr;
    eastl::unique_ptr<Texture2D> texHistoryDiffuse = nullptr;
    eastl::unique_ptr<Texture2D> texTemporal = nullptr;
    eastl::unique_ptr<Texture2D> texMoments = nullptr;
    eastl::unique_ptr<Texture2D> texHistoryMoments = nullptr;
    eastl::unique_ptr<Texture2D> texHistoryMomentsDiffuse = nullptr;
    eastl::unique_ptr<Texture2D> texHistoryNormals = nullptr;
    eastl::unique_ptr<Texture2D> texVariance = nullptr;
    eastl::unique_ptr<Texture2D> texOutput = nullptr;

#ifdef ENABLE_SHARC
    eastl::unique_ptr<Buffer> sharcHashEntries = nullptr;
    eastl::unique_ptr<Buffer> sharcHashCopyOffsets = nullptr;
    eastl::unique_ptr<Buffer> sharcVoxelData = nullptr;
    eastl::unique_ptr<Buffer> sharcVoxelDataPrev = nullptr;
#endif

    winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV = nullptr;

    static const uint maxMips = 9;
    static const uint sharcNumEntries = 0x100000;

    std::array<winrt::com_ptr<ID3D11ShaderResourceView>, maxMips> depthSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, maxMips> depthUAVs = { nullptr };

    winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> preprocessDepthCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchSpecularCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchDiffuseCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> prepareColorCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> depthDownsampleCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> diffuseCompositeCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> temporalCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> varianceCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> spatialCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> spatialSpecularCS = nullptr;
#ifdef ENABLE_SHARC
    winrt::com_ptr<ID3D11ComputeShader> raymarchDiffuseSharcCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> sharcUpdateRaymarchCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> sharcResolveCS = nullptr;
#endif
};
