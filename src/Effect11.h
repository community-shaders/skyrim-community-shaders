#pragma once

#include <d3d11.h>
#include <Effects11/d3dx11effect.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

/**
 * @brief Framework for loading and executing ENBSeries-compatible FX effect files
 * 
 * This class provides a complete framework for loading DirectX 11 Effect (.fx) files
 * compatible with ENBSeries, executing them with proper render target handling,
 * and managing shader techniques and variables.
 */
class Effect11
{
public:

	void Initialize();

	bool LoadFXFile(std::filesystem::path a_filePath);

    void ExecuteTechniqueSequence(const std::string& baseTechniqueName, ID3D11RenderTargetView* renderTarget);

private:
    ComPtr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<ComPtr<ID3DX11EffectTechnique>>> techniques;
	std::unordered_map<std::string, ComPtr<ID3DX11EffectVariable>> variables;
	std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> textureCache;

	ComPtr<ID3DX11EffectVariable> Timer;
	ComPtr<ID3DX11EffectVariable> ScreenSize;
	ComPtr<ID3DX11EffectVariable> AdaptiveQuality;
	ComPtr<ID3DX11EffectVariable> Weather;
	ComPtr<ID3DX11EffectVariable> TimeOfDay1;
	ComPtr<ID3DX11EffectVariable> TimeOfDay2;
	ComPtr<ID3DX11EffectVariable> ENightDayFactor;
	ComPtr<ID3DX11EffectVariable> EInteriorFactor;

	ComPtr<ID3DX11EffectVariable> Params01;
	ComPtr<ID3DX11EffectVariable> ENBParams01;
    
    ComPtr<ID3D11Buffer> quadVertexBuffer;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    ComPtr<ID3D11BlendState> blendState;

    void CreateQuadGeometry();
    void CreateRenderStates();
    std::vector<uint8_t> LoadFileToMemory(const std::string& filePath);

    void SetupCommonVariables();
    void UpdateCommonVariables();

	void SetupEffectVariables();
	void UpdateEffectVariables();

    void LoadResourceNameTextures();
    ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& filename);
    std::string GetResourceNameFromVariable(ID3DX11EffectVariable* variable);
};