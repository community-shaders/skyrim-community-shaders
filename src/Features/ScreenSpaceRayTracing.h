#pragma once

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
            {
                "Realistic indirect lighting",
                "Importance sampling for advanced reflections based on roughness",
                "Efficient ray marching with Hi-Z buffer",
                "Uses dynamic cubemaps as fallback for missing information"
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
        bool EnableSpecular = true;
        uint MaxSteps = 128;
        uint MaxMips = 6;
        float Thickness = 5.f;
        float NormalBias = 0.1f;
        float BRDFBias = 0.25f;
        bool UseDynamicCubemapsAsFallback = true;
        bool UseDynamicCubemapsAsFallbackSpecular = true;
        uint DiffuseSPP = 2;
        bool EnableDiffuse = true;
        float SpecularMult = 1.0f;
        float DiffuseMult = 1.0f;
        float AmbientMult = 0.0f;
        float OcclusionStrength = 1.0f;
        float CubemapNormalization = 0.0f;
    } settings;

    struct alignas(16) SharedData
    {
        uint EnableSpecular;
        float SpecularMult;
        float DiffuseMult;
        float AmbientMult;
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
        float CubemapNormalization;

        float2 TexDim;
        float2 RcpTexDim;
        float2 FrameDim;
        float2 RcpFrameDim;
    };

    eastl::unique_ptr<ConstantBuffer> ssrtCB;

    bool recompileFlag = false;

    void DrawSSRTSpecular();
    void DrawSSRTDiffuse();
    virtual void Prepass() override;

    SharedData GetCommonBufferData();

    struct DiffuseOutput
    {
        ID3D11ShaderResourceView* sh[4];
    };
    DiffuseOutput GetDiffuseOutputTextures();

    eastl::unique_ptr<Texture2D> texDepth = nullptr;
    eastl::unique_ptr<Texture2D> texColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRTDiffuseColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRTDiffuseDirection = nullptr;
    eastl::unique_ptr<Texture2D> texSSRTDiffuseSH[4] = { nullptr };
    eastl::unique_ptr<Texture2D> texOutput = nullptr;
	eastl::unique_ptr<Texture2D> texHalfResDepth = nullptr;

    winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV = nullptr;

    static const uint maxMips = 3;

    std::array<winrt::com_ptr<ID3D11ShaderResourceView>, maxMips> depthSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, maxMips> depthUAVs = { nullptr };

    winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;
    winrt::com_ptr<ID3D11SamplerState> pointSampler = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> prefilterDepthCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> prefilterRadianceCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchSpecularCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchDiffuseCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> depthDownsampleCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> upscaleDiffuseCS = nullptr;
};
