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
        uint MaxSteps = 8;
        uint NumRays = 1;
        bool Glossy = true;
        float RoughnessMask = 0.8f;
    } settings;

    struct alignas(16) SSRCB
    {
        uint MaxSteps;
        uint NumRays;
        uint Glossy;
        float RoughnessMask;
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
    eastl::unique_ptr<Texture2D> texHitDistance = nullptr;

    std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5> depthSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> depthUAVs = { nullptr };

    winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> preprocessDepthCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> prepareColorCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> spdCS = nullptr;
};
