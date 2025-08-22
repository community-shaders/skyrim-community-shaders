#pragma once
#include "PostProcessFeature.h"

#include "Buffer.h"

struct ColorGrading : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Color Grading and Tone Mapping"; }
	virtual inline std::string GetDesc() const override { return "Color grading operations and multiple tone mapping options."; }
    virtual inline bool DisableInMainLoadingMenu() const override { return true; }

	virtual bool SupportsVR() { return true; }

    template <size_t N, typename T>
    constexpr auto make_array(T value) -> std::array<T, N>
    {
        std::array<T, N> a{};
        for (auto& x : a)
            x = value;
        return a;
    }

    struct ColorProfile
    {
        // std::array<float4, 3> asccdl = { float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 0.f, 0.f, 0.f, 0.f } };
        // std::array<float4, 3> liftgammagain = { float4{ 0.f, 0.f, 0.f, 0.f }, float4{ 0.f, 0.f, 0.f, 0.f }, float4{ 1.f, 1.f, 1.f, 1.f } };
        // float4 saturationHueInOutGamma = float4{ 1.f, 1.f, 1.f, 1.f };
        // float4 oklchSaturation = float4{ 1.f, 1.f, 0.f, 0.f };
        // std::array<float4, 7> oklchColorMixer = make_array<7>(float4{ 0.f, 1.f, 0.f, 0.f });
        std::array<float4, 22> params = {
            float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 0.f, 0.f, 0.f, 0.f },
            float4{ 0.f, 0.f, 0.f, 0.f }, float4{ 0.f, 0.f, 0.f, 0.f }, float4{ 1.f, 1.f, 1.f, 1.f },
            float4{ 1.f, 1.f, 1.f, 1.f }, float4{ 1.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f },
            float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f },
            float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f },
            float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 0.18f, 0.18f, 0.18f, 0.f }, float4{ 1.f, 65.f, 0.f, 0.f },
            float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 1.f, 1.f, 1.f, 0.f }, float4{ 1.f, 1.f, 1.f, 0.f },
            float4{ 0.f, 0.3f, 0.55f, 1.f }
        };
    };

    struct Settings
    {
        bool useToDInterior = false;
        bool skipLDR = false;
        std::array<ColorProfile, 8> profiles = {};
        std::string currentTonemapper = "Reinhard";
        std::array<float4, 2> tonemapParams = { float4{ 0.f, 0.f, 0.f, 0.f }, float4{ 0.f, 0.f, 0.f, 0.f } };
        float3 gameCinematicBlend = { 1.0f, 1.0f, 1.0f };
        float gameFadeBlend = 1.0f;
        float gameTintBlend = 1.0f;
        bool useLog = false;
        uint logType = 0;
        bool invertLog = false;
        bool enableTonemap = true;
        bool enableColorSpaceTransform = false;
        int inputColorSpace = 0;
        int processColorSpace = 0;
        int outputColorSpace = 0;
        std::array<float3, 3> colorSpaceTransform = { float3{ 1.0f, 1.0f, 1.0f }, float3{ 1.0f, 1.0f, 1.0f }, float3{ 1.0f, 1.0f, 1.0f } };
        std::array<float3, 3> invColorSpaceTransform = { float3{ 1.0f, 1.0f, 1.0f }, float3{ 1.0f, 1.0f, 1.0f }, float3{ 1.0f, 1.0f, 1.0f } };
    } settings;

    int tonemapperType = 1;

	// std::array<float4, 17> profiles[8]; // normal, dawn, sunrise, day, sunset, dusk, night, interior
    const std::array<std::string, 8> profileNames = {
        "Normal", "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "Interior"
    };

    enum class LogType : uint32_t {
        ACEScct = 1 << 0,
        ARRIlogC4 = 1 << 1,
        SonySLog3 = 1 << 2,
        Invert = 1 << 3
    };

    struct alignas(16) ColorCB {
        float4 asccdl[3];
        float4 liftgammagain[3]; // lift，gamma，gain
        float4 saturationHueInOutGamma;
        float4 oklchSaturation;
        float4 oklchColorMixer[7];
        float4 contrast;
        float4 pivot;
        float4 exposureTemperatureTint;
        float4 shadows;
        float4 midtones;
        float4 highlights;
        float4 shadowsHighlightsRange; // shadowBegin, shadowEnd, highlightBegin, highlightEnd

        float4 tonemapParams[2];
        float4 colorSpaceTransform[3];
        float4 invColorSpaceTransform[3];

        // game value
        float4 cinematic; // saturation, brightness, contrast
        float4 fade; // color
        float4 tint; // color

        uint logType;
        uint skipLDR;
        uint enableTonemap;
        uint enableColorSpaceTransform;
    };
	std::unique_ptr<ConstantBuffer> colorCB = nullptr;

	std::unique_ptr<Texture2D> texColor = nullptr;
    std::unique_ptr<Texture3D> texLUT = nullptr;

    static constexpr int LUTDim = 64;

	bool recompileFlag = true;
	winrt::com_ptr<ID3D11ComputeShader> colorgradingCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> lutgenCS = nullptr;

    winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};