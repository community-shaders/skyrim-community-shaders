#pragma once

#include "Effect.h"
#include "Downsampler.h"
#include <Effects11/d3dx11effect.h>
#include <d3d11.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class EffectManager
{
public:
	static EffectManager& GetSingleton();

	// Effect execution
	void ExecuteEffects(RE::BSGraphics::RenderTargetData& input,
		RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);

	// UI Integration
	void RenderImGui();

	// Lifecycle
	void Initialize();
	void RegisterEffects();

	void ApplyEffects();
	void LoadEffects();
	void SaveEffects();

	// Common variable management
	void UpdateAllCommonVariables();
	void UpdateCommonVariablesForEffect(ID3DX11Effect* effect);

	std::unordered_map<std::string, std::unique_ptr<Effect>> effects;

	// Common resources shared across effects
	void CreateCommonResources();

	// Shared D3D resources
	ComPtr<ID3D11Buffer> quadVertexBuffer;
	ComPtr<ID3D11InputLayout> inputLayout;
	ComPtr<ID3D11RasterizerState> rasterizerState;
	ComPtr<ID3D11BlendState> blendState;

	void CreateQuadGeometry();
	void CreateRenderStates();

	// Common textures and variables
	struct Texture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, Texture> commonTextureCache;

	// Common variable data (updated once, applied to all effects)
	struct CommonVariableData
	{
		float timer[4];
		float screenSize[4];
		float weather[4];
		float timeOfDay1[4];
		float timeOfDay2[4];
		float eNightDayFactor;
		float eInteriorFactor;
	} commonData;

	void CreateCommonTextures();
	void UpdateCommonData();

	// Downsampling support
	Downsampler& GetDownsampler() { return Downsampler::GetSingleton(); }
	const Downsampler::DownsampleChain& GetSharedDownsampleChain() const { return sharedDownsampleChain; }
	void PerformSharedDownsampling(RE::BSGraphics::RenderTargetData& input);

private:
	Downsampler::DownsampleChain sharedDownsampleChain;
};