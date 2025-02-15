#pragma once
#pragma warning(disable:4324)
#include "Buffer.h"
#include "PostProcessFeature.h"

struct VanillaImagespace : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Vanilla Imagespace"; }
	virtual inline std::string GetDesc() const override { return "Simple node to apply vanilla imagespace settings."; }
    struct Settings
	{
		float3 blendFactor = float3(1.0f, 1.0f, 1.0f);
        float3 InteriorMultiplier = float3(1.0f, 1.0f, 1.0f);
        float3 ExteriorMultiplier = float3(1.0f, 1.0f, 1.0f);
        float3 InteriorOverride = float3(1.0f, 1.0f, 1.0f);
        float3 ExteriorOverride = float3(1.0f, 1.0f, 1.0f);
        bool enableInExMultiplier = false;
        bool enableInExOverride = false;
        uint8_t pad[2];
	} settings;

    struct alignas(16) VanillaImagespaceCB
    {
        float3 cinematic;
        float width;
        float height;
        uint8_t pad[12];
    };

    eastl::unique_ptr<ConstantBuffer> vanillaImagespaceCB = nullptr;

    eastl::unique_ptr<Texture2D> texOutput = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> vanillaImagespaceCS = nullptr;

    winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;

    VanillaImagespaceCB vanillaImagespaceData;

    float3 actualValues = float3(1.0f, 1.0f, 1.0f);
    bool isInInterior = false;

    virtual void SetupResources() override;
    virtual void ClearShaderCache() override;
    void CompileComputeShaders();

    virtual void RestoreDefaultSettings() override;
    virtual void LoadSettings(json&) override;
    virtual void SaveSettings(json&) override;

    virtual void DrawSettings() override;

    virtual void Draw(TextureInfo&) override;
};
