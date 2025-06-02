#pragma once
#pragma warning(disable : 4324)

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
        uint EyeIndex;
        float RoughnessMask;
        float pad[3];
    };

    SSRCB ssrCBData = {};

    eastl::unique_ptr<ConstantBuffer> ssrCB;
    
    void DrawSSR();

    eastl::unique_ptr<Texture2D> texDepth = nullptr;
    eastl::unique_ptr<Texture2D> texColor = nullptr;
    eastl::unique_ptr<Texture2D> texSSRColor = nullptr;
    eastl::unique_ptr<Texture2D> texHitDistance = nullptr;

    winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> preprocessDepthCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> raymarchCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> prepareColorCS = nullptr;
};
