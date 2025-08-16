#pragma once

#include "Effect.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <filesystem>
#include <d3d11.h>
#include <Effects11/d3dx11effect.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class EffectManager
{
public:
    static EffectManager& GetSingleton();

    // Effect registration and management
    void RegisterAllKnownEffects();
    
    bool LoadEffect(const std::string& name, const std::filesystem::path& filePath);
    void UnloadEffect(const std::string& name);
    void UnloadAllEffects();

    // Effect execution
    void ExecuteEffect(const std::string& name, RE::BSGraphics::RenderTargetData& input, 
                      RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);
    void ExecuteAllEffects(RE::BSGraphics::RenderTargetData& input, 
                          RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);
    
    // UI Integration
    void RenderImGui();
    
    // Lifecycle
    void Initialize();
    void Reset();
    
    // Common variable management
    void UpdateAllCommonVariables();
    void UpdateCommonVariablesForEffect(ID3DX11Effect* effect);

    struct EffectEntry {
        std::unique_ptr<Effect> effect;
        std::string type;
        bool isLoaded = false;
        bool isEnabled = true;
    };

    std::unordered_map<std::string, EffectEntry> effects;

    // Common resources shared across effects
    void InitializeSharedResources();
    void CleanupSharedResources();
    
    // Shared D3D resources
    ComPtr<ID3D11Buffer> sharedQuadVertexBuffer;
    ComPtr<ID3D11InputLayout> sharedInputLayout;
    ComPtr<ID3D11RasterizerState> sharedRasterizerState;
    ComPtr<ID3D11BlendState> sharedBlendState;
    
    void CreateSharedQuadGeometry();
    void CreateSharedRenderStates();
    
    // Common textures and variables
    struct Texture {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
    };
    
    std::unordered_map<std::string, Texture> commonTextureCache;
    
    // Common variable data (updated once, applied to all effects)
    struct CommonVariableData {
        float timer[4];
        float screenSize[4];
        float weather[4];
        float timeOfDay1[4];
        float timeOfDay2[4];
        float eNightDayFactor;
        float eInteriorFactor;
    } commonData;
    
    void SetupCommonTextures();
    void UpdateCommonData();
};