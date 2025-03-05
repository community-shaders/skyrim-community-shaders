#pragma once
// Disable warning about structure padding due to alignment specifier
#pragma warning(disable: 4324)

/**
 * Motion Blur Effect
 * 
 * High-quality motion blur using a three-pass compute shader approach:
 * 1. Tile max pass: Computes maximum velocity per tile
 * 2. Neighbor pass: Computes neighborhood max velocities between tiles
 * 3. Blur pass: Applies the blur based on velocity information
 * 
 * Features camera motion reduction to separate object motion from camera movement.
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
    
    // Static constants for hardcoded values
    static constexpr float MaxBlurRadius = 40.0f;
    static constexpr float DepthBiasFactor = 1.5f;
    static constexpr int UseDepthBounds = 1;
    
    // Core settings
    struct Settings {
        // Core settings
        int TileSize = 32;                 // Size of each tile for processing
        float VelocityScale = 100.0f;      // Motion vector scale
        float BlurScale = 1.0f;            // Overall blur strength
        int SampleCount = 12;              // Sample count for quality
        int VisualizationMode = 0;         // Visualization mode (0-5)
        float CameraMotionReduction = 0.0f;// Camera motion control
        
        // Debug options
        bool ShowNeighborMax = false;      // Show max velocity debug view
        bool ApplyBlur = true;             // Apply blur vs. just visualization
    };
    Settings settings;
    
    // D3D Resources
    winrt::com_ptr<ID3D11SamplerState> linearSampler;
    winrt::com_ptr<ID3D11SamplerState> pointSampler;
    
    // Compute shaders for the three-pass implementation
    winrt::com_ptr<ID3D11ComputeShader> tileMaxPassShader;     // Pass 1: Calculate max velocity per tile
    winrt::com_ptr<ID3D11ComputeShader> neighborMaxPassShader; // Pass 2: Calculate neighbor max velocities
    winrt::com_ptr<ID3D11ComputeShader> blurPassShader;        // Pass 3: Apply final blur
    
    // Buffers
    winrt::com_ptr<ID3D11Buffer> blurConstantBuffer;           // Main constant buffer for blur settings
    winrt::com_ptr<ID3D11Buffer> tilePassConstantBuffer;       // Simpler constant buffer for tile passes
    
    // Textures
    eastl::unique_ptr<Texture2D> tileMaxTexture;               // Downsampled texture for tile velocities
    eastl::unique_ptr<Texture2D> neighborMaxTexture;           // Texture for neighbor max velocities
    eastl::unique_ptr<Texture2D> blurOutputTexture;            // Full-resolution output texture
    
    // Resource tracking
    uint32_t lastWidth = 0;                                    // Last render width for resource resizing
    uint32_t lastHeight = 0;                                   // Last render height for resource resizing
    
    // Constant buffer structs
    struct alignas(16) MotionBlurConstantBuffer {              // For final blur pass
        uint32_t TileSize;
        float VelocityScale;
        float BlurScale;
        int32_t SampleCount;
        int32_t VisualizationMode;
        float CameraMotionReduction;
        float PaddingX;
        float PaddingY;
    };
    
    struct alignas(16) TilePassConstantBuffer {                // For tile and neighbor passes
        uint32_t TileSize;
        float VelocityScale;
        float PaddingX;
        float PaddingY;
    };
    
    // Constant buffer instances
    MotionBlurConstantBuffer motionBlurCB;
    TilePassConstantBuffer tilePassCB;
    
    // Cache for optimization
    MotionBlurConstantBuffer lastMotionBlurCB = {};
    TilePassConstantBuffer lastTilePassCB = {};
    
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
    void ExecuteTileMaxPass();
    void ExecuteNeighborMaxPass();
    void ExecuteBlurPass(TextureInfo& inout_tex);
    void SetVisualizationOutput(TextureInfo& inout_tex);
};