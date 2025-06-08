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
    virtual inline std::string_view GetShaderDefineName() override { return "SSSR"; }

    virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

    struct Settings
    {
        bool Enabled = true;
        uint MaxSteps = 128;
        uint MaxMips = 6;
        float Thickness = 5.f;
        float RoughnessMask = 0.8f;
        float BRDFBias = 0.25f;
        int SpatialTimes = 0;
        float SpatialRadius = 0.5f;
        bool EnableTemporal = false;
        float TemporalScale = 1.25f;
        float TemporalWeight = 0.92f;
        bool EnableBilateral = false;
        float BilateralScale = 0.5f;
        float BilateralColorWeight = 0.1f;
        float BilateralDepthWeight = 0.1f;
        float BilateralNormalWeight = 0.1f;
    } settings;

    struct alignas(16) SSRCB
    {
        uint MaxSteps;
        uint MaxMips;
        float Thickness;
        float SpatialRadius;
        float RoughnessMask;
        float TemporalScale;
        float TemporalWeight;
        float BilateralScale;
        float ColorWeight;
        float DepthWeight;
        float NormalWeight;
        float BRDFBias;
    };

    struct alignas(16) SPDCB
    {
        uint srcDimensions[2];
        uint numMips;
        uint slice;  // unused
        uint workGroupOffset[2];
        uint numWorkGroups;
        uint _padding;
    };

    eastl::unique_ptr<ConstantBuffer> ssrCB;
    eastl::unique_ptr<ConstantBuffer> spdCB;
    
    void DrawSSR();

    eastl::unique_ptr<Texture2D> texDepth = nullptr;
    eastl::unique_ptr<Texture2D> texColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRColor = nullptr;
    eastl::unique_ptr<Texture2D> texHitPDF = nullptr;
    eastl::unique_ptr<Texture2D> texSpatial = nullptr;
    eastl::unique_ptr<Texture2D> texTemporal = nullptr;
    eastl::unique_ptr<Texture2D> texBilateral = nullptr;
    eastl::unique_ptr<Texture2D> texHistory = nullptr;
    eastl::unique_ptr<Texture2D> texOutput = nullptr;

    winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV = nullptr;

    static const uint maxMips = 9;

    std::array<winrt::com_ptr<ID3D11ShaderResourceView>, maxMips> depthSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, maxMips> depthUAVs = { nullptr };

    winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> preprocessDepthCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> prepareColorCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> spdCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> spatialCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> temporalCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> bilateralCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> depthDownsampleCS = nullptr;
};
