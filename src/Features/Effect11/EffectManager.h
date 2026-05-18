#pragma once

#include "Effects/ENBAdaptation.h"
#include "Effects/ENBBloom.h"
#include "Effects/ENBEffect.h"
#include "Effects/ENBEffectPostPass.h"
#include "Effects/ENBLens.h"

enum class TimeOfDay1Index : int
{
	Dawn,
	Sunrise,
	Day,
	Sunset
};

enum class TimeOfDay2Index : int
{
	Dusk,
	Night,
	InteriorDay,
	InteriorNight
};

enum class TimeOfDayFactorIndex : int
{
	Dawn,
	Sunrise,
	Day,
	Sunset,
	Dusk,
	Night,
	Count
};

class EffectManager
{
public:
	static EffectManager& GetSingleton();

	// Effect execution
	void ExecuteEffects();

	// Lifecycle
	void Initialize();

	void Apply();
	void Load();
	void Save();

	void RegisterSettings();

	// Common variable management
	void UpdateCommonVariablesForEffect(ID3DX11Effect* effect);

public:
	ENBBloom enbBloom;
	ENBLens enbLens;
	ENBAdaptation enbAdaptation;
	ENBEffect enbEffect;
	ENBEffectPostPass enbEffectPostPass;

	// Common resources shared across effects
	void CreateCommonResources();

	// Shared D3D resources
	winrt::com_ptr<ID3D11Buffer> quadVertexBuffer;
	winrt::com_ptr<ID3D11InputLayout> inputLayout;
	winrt::com_ptr<ID3D11RasterizerState> rasterizerState;
	winrt::com_ptr<ID3D11BlendState> blendState;

	// Copy shader resources
	winrt::com_ptr<ID3D11VertexShader> copyVertexShader;
	winrt::com_ptr<ID3D11PixelShader> copyPixelShader;
	winrt::com_ptr<ID3D11Buffer> ditherConstantBuffer;

	// Color correction compute shader resources
	winrt::com_ptr<ID3D11ComputeShader> colorCorrectionComputeShader;
	winrt::com_ptr<ID3D11Buffer> colorCorrectionConstantBuffer;

	static std::string LoadShaderFile(const char* path);
	void CreateQuadGeometry();
	void CreateRenderStates();
	void CreateCopyShaders();
	void CreateColorCorrectionShader();

	void RenderEffectsList();

	// Common variable data (updated once, applied to all effects)
	struct CommonVariableData
	{
		float timer[4];
		float weather[4];
		float timeOfDay1[4];
		float timeOfDay2[4];
		float eNightDayFactor;
		float eInteriorFactor;
	} commonData;
	uint32_t frameCount = 0;

	void UpdateCommonData();

	struct SettingIDs
	{
		uint32_t useBloom = 0xFFFFFFFF;
		uint32_t useLens = 0xFFFFFFFF;
		uint32_t useAdaptation = 0xFFFFFFFF;
		uint32_t usePostPass = 0xFFFFFFFF;

		uint32_t enableMultipleWeathers = 0xFFFFFFFF;
		uint32_t enableLocationWeather = 0xFFFFFFFF;

		uint32_t nightTime = 0xFFFFFFFF;
		uint32_t sunriseTime = 0xFFFFFFFF;
		uint32_t dawnDuration = 0xFFFFFFFF;
		uint32_t dayTime = 0xFFFFFFFF;
		uint32_t sunsetTime = 0xFFFFFFFF;
		uint32_t duskDuration = 0xFFFFFFFF;

		uint32_t brightness = 0xFFFFFFFF;
		uint32_t gammaCurve = 0xFFFFFFFF;
	} ids;

	const CommonVariableData& GetCommonData() const { return commonData; }

	bool IsInitialized() const { return initialized; }

	bool performanceMode = false;

	// Execute a single effect with perf events and common variable setup
	void ExecuteEffect(Effect& effect, uint32_t enableSettingID = 0xFFFFFFFF);

	// Texture copy using pixel shader
	void CopyTexture(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* destination);

	// Color correction using compute shader
	void ApplyColorCorrection(ID3D11UnorderedAccessView* textureUAV);

	void ReloadShaders();

	// Error reporting for overlay display
	uint32_t GetFailedEffectCount() const;
	std::vector<std::string> GetAllErrors() const;

private:
	bool initialized = false;
};