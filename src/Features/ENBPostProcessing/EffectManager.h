#pragma once

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

	Effect::Texture* GetCommonTexture(const std::string& name);

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

	std::vector<std::pair<std::string, std::unique_ptr<Effect>>> effects;

	// Common resources shared across effects
	void CreateCommonResources();

	// Shared D3D resources
	ComPtr<ID3D11Buffer> quadVertexBuffer;
	ComPtr<ID3D11InputLayout> inputLayout;
	ComPtr<ID3D11RasterizerState> rasterizerState;
	ComPtr<ID3D11BlendState> blendState;

	// Copy shader resources
	ComPtr<ID3D11PixelShader> copyPixelShader;

	void CreateQuadGeometry();
	void CreateRenderStates();
	void CreateCopyShaders();

	std::unordered_map<std::string, Effect::Texture> commonTextureCache;

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

	// Texture copy using pixel shader
	void CopyTexture(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* destination);

	// Downsampling support
	Downsampler& GetDownsampler() { return Downsampler::GetSingleton(); }
	const Downsampler::DownsampleChain& GetSharedDownsampleChain() const { return sharedDownsampleChain; }

	// Common texture access

	const std::unordered_map<std::string, Effect::Texture>& GetAllCommonTextures() const { return commonTextureCache; }

private:
	Downsampler::DownsampleChain sharedDownsampleChain;
	
	// Time of day setting helper
	struct TimeOfDaySettings {
		float Dawn = 1.0f;
		float Sunrise = 1.0f;
		float Day = 1.0f;
		float Sunset = 1.0f;
		float Dusk = 1.0f;
		float Night = 1.0f;
		
		float& operator[](const std::string& timeOfDay) {
			if (timeOfDay == "Dawn") return Dawn;
			if (timeOfDay == "Sunrise") return Sunrise;
			if (timeOfDay == "Day") return Day;
			if (timeOfDay == "Sunset") return Sunset;
			if (timeOfDay == "Dusk") return Dusk;
			if (timeOfDay == "Night") return Night;
			return Dawn; // fallback
		}
	};

	// ENB settings storage
	struct ENBSettings {
		struct {
			float Brightness = 1.0f;
			float GammaCurve = 1.0f;
		} COLORCORRECTION;
		
		struct {
			float AdaptationSensitivity = 1.0f;
			bool ForceMinMaxValues = false;
			float AdaptationMin = 0.0f;
			float AdaptationMax = 1.0f;
		} ADAPTATION;
		
		struct {
			float FocusingTime = 1.0f;
			float ApertureTime = 1.0f;
		} DEPTHOFFIELD;
		
		struct {
			TimeOfDaySettings Amount;
		} BLOOM;
		
		struct {
			TimeOfDaySettings Amount;
		} LENS;
	} enbSettings;
	
	// Settings management
	void LoadENBSettings();
	void SaveENBSettings();
	void RenderTimeOfDaySettings(const std::string& prefix, TimeOfDaySettings& settings);
};