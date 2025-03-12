#pragma once
// Disable warning about structure padding due to alignment specifier
#pragma warning(disable: 4324)

/**
 * Motion Blur Effect
 * 
 * Three-pass compute shader approach based on CoD:AW:
 * 1. Reduction: Horizontal pass to [grid×height], then vertical to [grid×grid]
 * 2. Neighbor: Calculate neighborhood max velocities in 3×3 grid groups
 * 3. Blur: Apply motion blur using neighborhood velocities and depth comparison
 */

#include "PostProcessFeature.h"
#include "../../Buffer.h"
#include <EASTL/unique_ptr.h>

struct MotionBlur : public PostProcessFeature {
    // Feature interface
    inline std::string GetType() const override { return "Motion Blur"; }
    inline std::string GetDesc() const override { 
        return "Creates cinematic motion blur based on camera and object movement."; 
    }
    
    // Constants
    static constexpr float MaxBlurRadius = 40.0f;
    static constexpr float DepthBiasFactor = 1.5f;
    static constexpr int UseDepthBounds = 1;
    static constexpr int FixedGridSize = 20;
    
    // Motion scale presets
    enum class MotionScale {
        VeryShort = 0,
        Short = 1,
        Medium = 2,
        Long = 3,
        VeryLong = 4
    };
    
    // Settings
    struct Settings {
        float VelocityScale = 300.0f; // Will be mapped from MotionScale
        int SampleCount = 8;          // Doubled internally before sending to shader
        MotionScale ScalePreset = MotionScale::Medium;
    };
    Settings settings;
    
    // Function to map MotionScale enum to actual velocity scale value
    float GetScaleValueFromPreset(MotionScale preset) const {
        switch (preset) {
            case MotionScale::VeryShort: return 100.0f;
            case MotionScale::Short: return 200.0f;
            case MotionScale::Medium: return 300.0f;
            case MotionScale::Long: return 400.0f;
            case MotionScale::VeryLong: return 500.0f;
            default: return 300.0f;
        }
    }
    
    // D3D Resources
    winrt::com_ptr<ID3D11SamplerState> linearSampler;
    winrt::com_ptr<ID3D11SamplerState> pointSampler;
    
    // Compute shaders
    winrt::com_ptr<ID3D11ComputeShader> horizontalPassShader; // Pass 1a
    winrt::com_ptr<ID3D11ComputeShader> verticalPassShader;    // Pass 1b
    winrt::com_ptr<ID3D11ComputeShader> neighborMaxPassShader; // Pass 2
    winrt::com_ptr<ID3D11ComputeShader> blurPassShader;        // Pass 3
    
    // Constant buffers
    winrt::com_ptr<ID3D11Buffer> blurConstantBuffer;
    winrt::com_ptr<ID3D11Buffer> reductionPassConstantBuffer;
    
    // Textures
    eastl::unique_ptr<Texture2D> horizontalPassTexture;  // [grid x height]
    eastl::unique_ptr<Texture2D> verticalPassTexture;    // [grid x grid]
    eastl::unique_ptr<Texture2D> neighborMaxTexture;     // [grid x grid]
    eastl::unique_ptr<Texture2D> blurOutputTexture;      // Full resolution
    
    // Dimensions tracking
    uint32_t lastWidth = 0;
    uint32_t lastHeight = 0;
    
    // Constant buffer structs
    struct alignas(16) MotionBlurConstantBuffer {
        float VelocityScale;
        int32_t SampleCount;
    };
    
    struct alignas(16) ReductionPassConstantBuffer {
        float VelocityScale;
    };
    
    // CB instances
    MotionBlurConstantBuffer motionBlurCB;
    ReductionPassConstantBuffer reductionPassCB;
    
    // Cache for optimization
    MotionBlurConstantBuffer lastMotionBlurCB = {};
    ReductionPassConstantBuffer lastReductionPassCB = {};
    
    // Interface methods
    void SetupResources() override;
    void ClearShaderCache() override;
    void RestoreDefaultSettings() override;
    void LoadSettings(json&) override;
    void SaveSettings(json&) override;
    void DrawSettings() override;
    void Draw(TextureInfo&) override;
    
    // Helper methods
    void CompileComputeShaders();
    bool CheckAndResizeResources(const TextureInfo& inout_tex);
    bool UpdateConstantBuffers();
    void SetupComputePass(ID3D11ComputeShader* shader, 
                         ID3D11ShaderResourceView** srvs, 
                         uint32_t srvCount,
                         ID3D11UnorderedAccessView* uav, 
                         ID3D11Buffer* constantBuffer);
    void ClearComputeResources(uint32_t srvCount = 1);
    void ExecuteHorizontalPass();
    void ExecuteVerticalPass();
    void ExecuteNeighborMaxPass();
    void ExecuteBlurPass(TextureInfo& inout_tex);
};