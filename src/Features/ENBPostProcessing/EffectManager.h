#pragma once

#include "ENBDownsampler.h"
#include "SettingsRegistry.h"
#include "WeatherManager.h"
#include "TextureManager.h"
#include "ENBTexture.h"
#include "ENBDepthOfField.h"
#include "ENBBloom.h"
#include "ENBLens.h"
#include "ENBAdaptation.h"
#include "ENBEffect.h"
#include "ENBEffectPostPass.h"
#include <Effects11/d3dx11effect.h>
#include <d3d11.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include <Features/ENBPostProcessing/Effect.h>

using Microsoft::WRL::ComPtr;

// Forward declarations
class Effect;

class EffectManager
{
public:
	static EffectManager& GetSingleton();

	// Effect execution
	void ExecuteEffects();

	// UI Integration
	void RenderImGui();

	// Lifecycle
	void Initialize();
	void RegisterEffects();

	void ApplyEffects();
	void LoadEffects();
	void SaveEffects();

	// Common variable management
	void UpdateCommonVariablesForEffect(ID3DX11Effect* effect);

public:
	// Direct effect instances
	ENBDepthOfField enbDepthOfField;
	ENBBloom enbBloom;
	ENBLens enbLens;
	ENBAdaptation enbAdaptation;
	ENBEffect enbEffect;
	ENBEffectPostPass enbEffectPostPass;

	std::vector<std::pair<std::string, std::unique_ptr<Effect>>> effects;

	// Common resources shared across effects
	void CreateCommonResources();

	// Shared D3D resources
	ComPtr<ID3D11Buffer> quadVertexBuffer;
	ComPtr<ID3D11InputLayout> inputLayout;
	ComPtr<ID3D11RasterizerState> rasterizerState;
	ComPtr<ID3D11BlendState> blendState;

	// Copy shader resources
	ComPtr<ID3D11VertexShader> copyVertexShader;
	ComPtr<ID3D11PixelShader> copyPixelShader;

	// Color correction compute shader resources
	ComPtr<ID3D11ComputeShader> colorCorrectionComputeShader;
	ComPtr<ID3D11Buffer> colorCorrectionConstantBuffer;

	void CreateQuadGeometry();
	void CreateRenderStates();
	void CreateCopyShaders();
	void CreateColorCorrectionShader();

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

	void UpdateCommonData();

	// Texture copy using pixel shader
	void CopyTexture(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* destination);

	// Color correction using compute shader
	void ApplyColorCorrection(ID3D11UnorderedAccessView* textureUAV);

	// Downsampling support
	ENBDownsampler& GetDownsampler() { return ENBDownsampler::GetSingleton(); }

	// Texture swap tracking
	uint32_t textureSwap = 0;

	// Settings management (now delegated to SettingsRegistry)
	void LoadENBSettings();
	void SaveENBSettings();
	void LoadAllWeatherSettings();
	void SaveAllWeatherSettings();

	// Helper methods for backwards compatibility
	float GetInterpolatedBloomAmount();
	float GetInterpolatedLensAmount();

	// Settings access helpers
	template <typename T>
	T GetSetting(const std::string& key, const std::string& category)
	{
		return SettingsRegistry::GetSingleton().GetValue<T>(key, category);
	}

	template <typename T>
	void SetSetting(const std::string& key, const std::string& category, const T& value)
	{
		SettingsRegistry::GetSingleton().SetValue<T>(key, category, value);
	}
};