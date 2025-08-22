#pragma once

#include "Effects/ENBAdaptation.h"
#include "Effects/ENBBloom.h"
#include "Effects/ENBDepthOfField.h"
#include "Effects/ENBEffect.h"
#include "Effects/ENBEffectPostPass.h"
#include "Effects/ENBLens.h"

using Microsoft::WRL::ComPtr;

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
	// Direct effect instances
	ENBDepthOfField enbDepthOfField;
	ENBBloom enbBloom;
	ENBLens enbLens;
	ENBAdaptation enbAdaptation;
	ENBEffect enbEffect;
	ENBEffectPostPass enbEffectPostPass;

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

	void RenderEffectsList();

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
	const CommonVariableData& GetCommonData() const { return commonData; }

	// Texture copy using pixel shader
	void CopyTexture(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* destination);

	// Color correction using compute shader
	void ApplyColorCorrection(ID3D11UnorderedAccessView* textureUAV);
};