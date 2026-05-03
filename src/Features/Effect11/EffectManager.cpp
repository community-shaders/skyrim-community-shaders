#include "EffectManager.h"

#include "State.h"

#include "SettingManager.h"
#include "TextureManager.h"
#include "WeatherManager.h"

#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <vector>

EffectManager& EffectManager::GetSingleton()
{
	static EffectManager instance;
	return instance;
}

void EffectManager::Initialize()
{
	TextureManager::GetSingleton().Initialize();
	RegisterSettings();
	SettingManager::GetSingleton().Load();
	CreateCommonResources();
	Apply();

	// Verify all critical common resources are initialized correctly
	struct ResourceCheck
	{
		const void* resource;
		const char* name;
	};

	const ResourceCheck checks[] = {
		{ quadVertexBuffer.get(), "quadVertexBuffer" },
		{ inputLayout.get(), "inputLayout" },
		{ rasterizerState.get(), "rasterizerState" },
		{ blendState.get(), "blendState" },
		{ copyVertexShader.get(), "copyVertexShader" },
		{ copyPixelShader.get(), "copyPixelShader" },
		{ colorCorrectionComputeShader.get(), "colorCorrectionComputeShader" },
		{ colorCorrectionConstantBuffer.get(), "colorCorrectionConstantBuffer" },
	};

	bool resourcesValid = true;
	for (const auto& [resource, name] : checks) {
		if (!resource) {
			logger::error("[EffectManager] {} failed to initialize", name);
			resourcesValid = false;
		}
	}

	if (!resourcesValid) {
		logger::error("[EffectManager] Initialization failed due to missing resources");
		initialized = false;
	} else {
		initialized = true;
	}
}

void EffectManager::Apply()
{
	enbBloom.Apply();
	enbLens.Apply();
	enbAdaptation.Apply();
	enbEffect.Apply();
	enbEffectPostPass.Apply();
}

void EffectManager::Load()
{
	enbBloom.Load();
	enbLens.Load();
	enbAdaptation.Load();
	enbEffect.Load();
	enbEffectPostPass.Load();
}

void EffectManager::Save()
{
	enbBloom.Save();
	enbLens.Save();
	enbAdaptation.Save();
	enbEffect.Save();
	enbEffectPostPass.Save();
}

void EffectManager::RegisterSettings()
{
	auto& settingManager = SettingManager::GetSingleton();

	// GLOBAL
	settingManager.RegisterBoolSetting("UseEffect", "GLOBAL", false, false);

	// TIMEOFDAY
	settingManager.RegisterFloatSetting("DawnDuration", "TIMEOFDAY", 1.6f, 0.1f, 6.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("SunriseTime", "TIMEOFDAY", 9.0f, 2.0f, 12.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("DayTime", "TIMEOFDAY", 12.0f, 0.0f, 24.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("SunsetTime", "TIMEOFDAY", 17.25f, 0.0f, 23.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("DuskDuration", "TIMEOFDAY", 2.0f, 0.1f, 6.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("NightTime", "TIMEOFDAY", 1.0f, 0.0f, 24.0f, 0.01f, false);

	// WEATHER
	settingManager.RegisterBoolSetting("EnableMultipleWeathers", "WEATHER", true, false);
	settingManager.RegisterBoolSetting("EnableLocationWeather", "WEATHER", true, false);

	// COLORCORRECTION
	settingManager.RegisterFloatSetting("Brightness", "COLORCORRECTION", 1.0f, 0.0f, 10000.0f, 0.001f, false);
	settingManager.RegisterFloatSetting("GammaCurve", "COLORCORRECTION", 1.0f, 1.0f, 2.5f, 0.01f, false);

	// EFFECT
	settingManager.RegisterBoolSetting("UseOriginalPostProcessing", "EFFECT", false, false);

	settingManager.RegisterBoolSetting("EnablePostPassShader", "EFFECT", false, false);
	settingManager.RegisterBoolSetting("EnableAdaptation", "EFFECT", true, false);
	settingManager.RegisterBoolSetting("EnableBloom", "EFFECT", true, false);
	settingManager.RegisterBoolSetting("EnableLens", "EFFECT", false, false);

	settingManager.RegisterBoolSetting("EnableCloudShadows", "EFFECT", true, false);
	settingManager.RegisterBoolSetting("EnableImageBasedLighting", "EFFECT", true, false);

	// ADAPTATION
	settingManager.RegisterFloatSetting("AdaptationSensitivity", "ADAPTATION", 1.0f, 0.0f, 1.0f, 0.01f, false);
	settingManager.RegisterBoolSetting("ForceMinMaxValues", "ADAPTATION", false, false);
	settingManager.RegisterFloatSetting("AdaptationMin", "ADAPTATION", 0.0f, 0.0f, 65536.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("AdaptationMax", "ADAPTATION", 1.0f, 0.0f, 65536.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("AdaptationTime", "ADAPTATION", 1.0f, 0.05f, 100.0f, 0.01f, false);

	// BLOOM
	settingManager.RegisterTimeOfDaySetting("Amount", "BLOOM", 1.0f, 0.0f, 10.0f, 0.01f, true);

	// LENS
	settingManager.RegisterTimeOfDaySetting("Amount", "LENS", 1.0f, 0.0f, 10.0f, 0.01f, true);

	// CLOUDSHADOWS
	settingManager.RegisterTimeOfDaySetting("Amount", "CLOUDSHADOWS", 1.0f, 0.0f, 4.0f, 0.01f, true);

	// SKY
	settingManager.RegisterBoolSetting("Enable", "SKY", true, false);
	settingManager.RegisterBoolSetting("DisableWrongSkyMath", "SKY", false, false);

	settingManager.RegisterTimeOfDaySetting("GradientIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("GradientDesaturation", "SKY", 0.0f, -1.0f, 1.0f, 0.01f, true);

	settingManager.RegisterTimeOfDaySetting("GradientTopIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("GradientTopCurve", "SKY", 1.0f, 0.1f, 8.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("GradientTopColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("GradientMiddleIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("GradientMiddleCurve", "SKY", 1.0f, 0.1f, 8.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("GradientMiddleColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("GradientHorizonIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("GradientHorizonCurve", "SKY", 1.0f, 0.1f, 8.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("GradientHorizonColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("CloudsIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("CloudsCurve", "SKY", 1.0f, 0.1f, 8.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("CloudsDesaturation", "SKY", 0.0f, -1.0f, 1.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("CloudsOpacity", "SKY", 1.0f, 0.0f, 5.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("CloudsColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("SunIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("SunDesaturation", "SKY", 0.0f, -1.0f, 1.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("SunColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("MoonIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("MoonDesaturation", "SKY", 0.0f, -1.0f, 1.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("MoonColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("StarsIntensity", "SKY", 1.0f, 0.0f, 30000.0f, 0.01f, true);

	settingManager.RegisterFloatSetting("CloudsEdgeIntensity", "SKY", 0.0f, 0.0f, 10.0f, 0.01f, false);
	settingManager.RegisterFloatSetting("CloudsEdgeMoonMultiplier", "SKY", 0.0f, 0.0f, 10.0f, 0.01f, false);

	// ENVIRONMENT
	settingManager.RegisterTimeOfDaySetting("DirectLightingIntensity", "ENVIRONMENT", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("DirectLightingCurve", "ENVIRONMENT", 1.0f, 0.1f, 8.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("DirectLightingDesaturation", "ENVIRONMENT", 0.0f, -1.0f, 1.0f, 0.01f, true);

	settingManager.RegisterTimeOfDaySetting("AmbientLightingIntensity", "ENVIRONMENT", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("AmbientLightingDesaturation", "ENVIRONMENT", 0.0f, -1.0f, 1.0f, 0.01f, true);

	settingManager.RegisterTimeOfDaySetting("PointLightingIntensity", "ENVIRONMENT", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("PointLightingCurve", "ENVIRONMENT", 1.0f, 0.1f, 4.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("PointLightingDesaturation", "ENVIRONMENT", 0.0f, -1.0f, 1.0f, 0.01f, true);

	settingManager.RegisterColorTimeOfDaySetting("DirectLightingColorFilter", "ENVIRONMENT", { 1.0f, 1.0f, 1.0f }, true);
	settingManager.RegisterTimeOfDaySetting("DirectLightingColorFilterAmount", "ENVIRONMENT", 0.0f, 0.0f, 1.0f, 0.01f, true);

	settingManager.RegisterTimeOfDaySetting("FogColorMultiplier", "ENVIRONMENT", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("FogColorCurve", "ENVIRONMENT", 1.0f, 0.0f, 8.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("FogAmountMultiplier", "ENVIRONMENT", 1.0f, 0.0f, 10.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("FogCurveMultiplier", "ENVIRONMENT", 1.0f, 0.0f, 10.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("FogColorFilter", "ENVIRONMENT", { 1.0f, 1.0f, 1.0f }, true);
	settingManager.RegisterTimeOfDaySetting("FogColorFilterAmount", "ENVIRONMENT", 0.0f, 0.0f, 1.0f, 0.01f, true);

	settingManager.RegisterTimeOfDaySetting("ColorPow", "ENVIRONMENT", 1.0f, 1.0f, 2.2f, 0.01f, true);

	// IMAGEBASEDLIGHTING
	settingManager.RegisterTimeOfDaySetting("MultiplicativeAmount", "IMAGEBASEDLIGHTING", 0.0f, 0.0f, 10.0f, 0.01f, true);

	// SUNGLARE
	settingManager.RegisterTimeOfDaySetting("GlowIntensity", "SUNGLARE", 1.0f, 0.0f, 1000.0f, 0.01f, true);

	// VOLUMETRICFOG
	settingManager.RegisterTimeOfDaySetting("Intensity", "VOLUMETRICFOG", 1.0f, 0.0f, 30000.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("ColorFilter", "VOLUMETRICFOG", { 1.0f, 1.0f, 1.0f }, true);

	// PARTICLE
	settingManager.RegisterTimeOfDaySetting("Intensity", "PARTICLE", 1.0f, 0.0f, 30000.0f, 0.01f, true);

	// GAMEVOLUMETRICRAYS
	settingManager.RegisterTimeOfDaySetting("Intensity", "GAMEVOLUMETRICRAYS", 1.0f, 0.0f, 1000.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("RangeFactor", "GAMEVOLUMETRICRAYS", 1.0f, 0.0f, 100.0f, 0.01f, true);
	settingManager.RegisterTimeOfDaySetting("Desaturation", "GAMEVOLUMETRICRAYS", 0.0f, -1.0f, 1.0f, 0.01f, true);
	settingManager.RegisterColorTimeOfDaySetting("ColorFilter", "GAMEVOLUMETRICRAYS", { 1.0f, 1.0f, 1.0f }, true);

	// Cache IDs for performance
	ids.useBloom = settingManager.GetSettingID("EnableBloom", "EFFECT");
	ids.useLens = settingManager.GetSettingID("EnableLens", "EFFECT");
	ids.useAdaptation = settingManager.GetSettingID("EnableAdaptation", "EFFECT");
	ids.usePostPass = settingManager.GetSettingID("EnablePostPassShader", "EFFECT");

	ids.enableMultipleWeathers = settingManager.GetSettingID("EnableMultipleWeathers", "WEATHER");
	ids.enableLocationWeather = settingManager.GetSettingID("EnableLocationWeather", "WEATHER");

	ids.nightTime = settingManager.GetSettingID("NightTime", "TIMEOFDAY");
	ids.sunriseTime = settingManager.GetSettingID("SunriseTime", "TIMEOFDAY");
	ids.dawnDuration = settingManager.GetSettingID("DawnDuration", "TIMEOFDAY");
	ids.dayTime = settingManager.GetSettingID("DayTime", "TIMEOFDAY");
	ids.sunsetTime = settingManager.GetSettingID("SunsetTime", "TIMEOFDAY");
	ids.duskDuration = settingManager.GetSettingID("DuskDuration", "TIMEOFDAY");

	ids.brightness = settingManager.GetSettingID("Brightness", "COLORCORRECTION");
	ids.gammaCurve = settingManager.GetSettingID("GammaCurve", "COLORCORRECTION");
}

void EffectManager::ExecuteEffect(Effect& a_effect, uint32_t enableSettingID)
{
	if (!a_effect.IsCompiled())
		return;

	if (enableSettingID != 0xFFFFFFFF && !SettingManager::GetSingleton().GetValue<bool>(enableSettingID))
		return;

	auto state = globals::state;
	state->BeginPerfEvent(a_effect.GetName());
	UpdateCommonVariablesForEffect(a_effect.GetEffect());
	a_effect.UpdateEffectVariables();
	a_effect.Execute();
	state->EndPerfEvent();
}

void EffectManager::ExecuteEffects()
{
	if (!initialized)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	if (!rasterizerState || !blendState || !quadVertexBuffer || !inputLayout || !renderer)
		return;

	// Save State
	ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };
	ID3D11DepthStencilView* oldDSV = nullptr;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, &oldDSV);

	D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, oldViewports);

	ID3D11RasterizerState* oldRS = nullptr;
	context->RSGetState(&oldRS);

	ID3D11BlendState* oldBlend = nullptr;
	FLOAT oldBlendFactor[4];
	UINT oldSampleMask;
	context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldSampleMask);

	ID3D11DepthStencilState* oldDepth = nullptr;
	UINT oldStencilRef;
	context->OMGetDepthStencilState(&oldDepth, &oldStencilRef);

	ID3D11InputLayout* oldInputLayout = nullptr;
	context->IAGetInputLayout(&oldInputLayout);

	D3D11_PRIMITIVE_TOPOLOGY oldTopology;
	context->IAGetPrimitiveTopology(&oldTopology);

	ID3D11Buffer* oldVBs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
	UINT oldStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = { 0 };
	UINT oldOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = { 0 };
	context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, oldVBs, oldStrides, oldOffsets);

	ID3D11Buffer* oldIB = nullptr;
	DXGI_FORMAT oldIBFormat;
	UINT oldIBOffset;
	context->IAGetIndexBuffer(&oldIB, &oldIBFormat, &oldIBOffset);

	ID3D11VertexShader* oldVS = nullptr;
	context->VSGetShader(&oldVS, nullptr, nullptr);

	ID3D11PixelShader* oldPS = nullptr;
	context->PSGetShader(&oldPS, nullptr, nullptr);

	ID3D11GeometryShader* oldGS = nullptr;
	context->GSGetShader(&oldGS, nullptr, nullptr);

	ID3D11HullShader* oldHS = nullptr;
	context->HSGetShader(&oldHS, nullptr, nullptr);

	ID3D11DomainShader* oldDS = nullptr;
	context->DSGetShader(&oldDS, nullptr, nullptr);

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	// Set our render state
	context->RSSetState(rasterizerState.get());
	context->OMSetBlendState(blendState.get(), nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(nullptr, 0);

	UINT stride = sizeof(float) * 5;
	UINT offset = 0;
	ID3D11Buffer* vertexBuffers[] = { quadVertexBuffer.get() };
	context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
	context->IASetInputLayout(inputLayout.get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// Apply brightness and gamma curve
	ApplyColorCorrection(textureOriginal.UAV);

	auto& textureManager = TextureManager::GetSingleton();

	// Downsampled texture shared between bloom, lens and adaptation
	textureManager.UpdateDownsampledTexture(textureOriginal.SRV);

	ExecuteEffect(enbBloom, ids.useBloom);
	ExecuteEffect(enbLens, ids.useLens);
	ExecuteEffect(enbAdaptation, ids.useAdaptation);
	ExecuteEffect(enbEffect);
	ExecuteEffect(enbEffectPostPass, ids.usePostPass);

	textureManager.IncrementTextureSwap();

	// Copy final render target to framebuffer
	auto* textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	if (textureSDRTemp) {
		auto textureFramebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
		CopyTexture(textureSDRTemp->srv.get(), textureFramebuffer.RTV);
	}

	// Restore State
	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV);
	context->RSSetViewports(numViewports, oldViewports);
	context->RSSetState(oldRS);
	context->OMSetBlendState(oldBlend, oldBlendFactor, oldSampleMask);
	context->OMSetDepthStencilState(oldDepth, oldStencilRef);

	context->IASetInputLayout(oldInputLayout);
	context->IASetPrimitiveTopology(oldTopology);
	context->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, oldVBs, oldStrides, oldOffsets);
	context->IASetIndexBuffer(oldIB, oldIBFormat, oldIBOffset);

	context->VSSetShader(oldVS, nullptr, 0);
	context->PSSetShader(oldPS, nullptr, 0);
	context->GSSetShader(oldGS, nullptr, 0);
	context->HSSetShader(oldHS, nullptr, 0);
	context->DSSetShader(oldDS, nullptr, 0);

	// Release acquired COM interfaces to prevent memory leaks
	if (oldRS)
		oldRS->Release();
	if (oldBlend)
		oldBlend->Release();
	if (oldDepth)
		oldDepth->Release();
	if (oldInputLayout)
		oldInputLayout->Release();
	if (oldIB)
		oldIB->Release();
	if (oldVS)
		oldVS->Release();
	if (oldPS)
		oldPS->Release();
	if (oldGS)
		oldGS->Release();
	if (oldHS)
		oldHS->Release();
	if (oldDS)
		oldDS->Release();
	if (oldDSV)
		oldDSV->Release();

	// Release arrays
	for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		if (oldRTVs[i])
			oldRTVs[i]->Release();
	}
	for (int i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) {
		if (oldVBs[i])
			oldVBs[i]->Release();
	}
}

std::string EffectManager::LoadShaderFile(const char* path)
{
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs.is_open()) {
		logger::error("[ENBPP] Failed to open shader file: {}", path);
		return {};
	}
	return { std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>() };
}

void EffectManager::CreateCommonResources()
{
	CreateQuadGeometry();
	CreateRenderStates();
	CreateCopyShaders();
	CreateColorCorrectionShader();
}

void EffectManager::CreateQuadGeometry()
{
	// Create a fullscreen quad vertex buffer that all effects can share
	struct QuadVertex
	{
		float position[3];
		float texCoord[2];
	};

	QuadVertex vertices[] = {
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },  // Bottom left
		{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },   // Top left
		{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },   // Bottom right
		{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } }     // Top right
	};

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(vertices);
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = vertices;

	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, quadVertexBuffer.put()));

	// Create input layout for ENB post-processing
	D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	auto vertexShaderSource = LoadShaderFile("Data\\Shaders\\Effect11\\QuadVS.hlsl");
	if (vertexShaderSource.empty())
		return;

	winrt::com_ptr<ID3DBlob> vertexShaderBlob;
	winrt::com_ptr<ID3DBlob> errorBlob;
	HRESULT hr = D3DCompile(vertexShaderSource.data(), vertexShaderSource.size(), "QuadVS.hlsl", nullptr, nullptr,
		"main", "vs_4_0", 0, 0, vertexShaderBlob.put(), errorBlob.put());

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("[ENBPP] Failed to compile input layout vertex shader: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	hr = globals::d3d::device->CreateInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs),
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize(),
		inputLayout.put());
	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create shared input layout for ENB effects");
	}
}

void EffectManager::CreateRenderStates()
{
	// Rasterizer state for fullscreen quads
	D3D11_RASTERIZER_DESC rastDesc = {};
	rastDesc.FillMode = D3D11_FILL_SOLID;
	rastDesc.CullMode = D3D11_CULL_NONE;
	rastDesc.FrontCounterClockwise = FALSE;
	rastDesc.DepthBias = 0;
	rastDesc.DepthBiasClamp = 0.0f;
	rastDesc.SlopeScaledDepthBias = 0.0f;
	rastDesc.DepthClipEnable = TRUE;
	rastDesc.ScissorEnable = FALSE;
	rastDesc.MultisampleEnable = FALSE;
	rastDesc.AntialiasedLineEnable = FALSE;

	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rastDesc, rasterizerState.put()));

	// Blend state for standard rendering (no blending)
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, blendState.put()));
}

void EffectManager::CreateCopyShaders()
{
	auto vertexShaderSource = LoadShaderFile("Data\\Shaders\\Effect11\\QuadVS.hlsl");
	if (vertexShaderSource.empty())
		return;

	winrt::com_ptr<ID3DBlob> vsBlob, errorBlob;
	HRESULT hr = D3DCompile(vertexShaderSource.data(), vertexShaderSource.size(), "QuadVS.hlsl", nullptr, nullptr,
		"main", "vs_4_0", 0, 0, vsBlob.put(), errorBlob.put());

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("[ENBPP] Failed to compile copy vertex shader: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	hr = globals::d3d::device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, copyVertexShader.put());
	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create copy vertex shader");
		return;
	}

	auto pixelShaderSource = LoadShaderFile("Data\\Shaders\\Effect11\\CopyPS.hlsl");
	if (pixelShaderSource.empty())
		return;

	winrt::com_ptr<ID3DBlob> psBlob;
	hr = D3DCompile(pixelShaderSource.data(), pixelShaderSource.size(), "CopyPS.hlsl", nullptr, nullptr,
		"main", "ps_4_0", 0, 0, psBlob.put(), errorBlob.put());

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("[ENBPP] Failed to compile copy pixel shader: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	hr = globals::d3d::device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, copyPixelShader.put());
	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create copy pixel shader");
		return;
	}

	logger::info("[ENBPP] Created texture copy shaders successfully");
}

void EffectManager::CreateColorCorrectionShader()
{
	auto computeShaderSource = LoadShaderFile("Data\\Shaders\\Effect11\\ColorCorrectionCS.hlsl");
	if (computeShaderSource.empty())
		return;

	winrt::com_ptr<ID3DBlob> csBlob, errorBlob;
	HRESULT hr = D3DCompile(computeShaderSource.data(), computeShaderSource.size(), "ColorCorrectionCS.hlsl", nullptr, nullptr,
		"main", "cs_5_0", 0, 0, csBlob.put(), errorBlob.put());

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("[ENBPP] Failed to compile color correction compute shader: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	hr = globals::d3d::device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, colorCorrectionComputeShader.put());
	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create color correction compute shader");
		return;
	}

	// Create constant buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.ByteWidth = sizeof(float) * 4;  // Brightness, GammaCurve, padding[2]
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = globals::d3d::device->CreateBuffer(&cbDesc, nullptr, colorCorrectionConstantBuffer.put());
	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create color correction constant buffer");
		return;
	}

	logger::info("[ENBPP] Created color correction compute shader successfully");
}

void EffectManager::UpdateCommonData()
{
	commonData = {};

	auto sky = globals::game::sky;

	// Update timer
	{
		auto delta = (*globals::game::deltaTime);

		static double timer = 0.0f;
		timer += delta;

		static uint frameCount = 0;

		auto modifiedTimer = std::fmodf(static_cast<float>(timer) * 1000.0f, 16777216);
		modifiedTimer /= 16777216.0f;

		commonData.timer[0] = modifiedTimer;
		commonData.timer[1] = 60.0f;
		commonData.timer[2] = static_cast<float>(frameCount % 9999);
		commonData.timer[3] = delta;

		frameCount++;
	}

	// Update weather
	{
		// Strip plugin index (2 leftmost digits) from form IDs
		auto stripPluginIndex = [](uint32_t formID) -> uint32_t {
			return formID & 0x00FFFFFF;  // Keep only the lower 6 hex digits
		};

		if (sky) {
			auto& weatherManager = WeatherManager::GetSingleton();
			uint32_t currentID = sky->currentWeather ? stripPluginIndex(sky->currentWeather->formID) : 0;
			uint32_t lastID = sky->lastWeather ? stripPluginIndex(sky->lastWeather->formID) : 0;

			commonData.weather[0] = static_cast<float>(weatherManager.GetEffectiveWeatherID(currentID));
			commonData.weather[1] = static_cast<float>(weatherManager.GetEffectiveWeatherID(lastID));
			commonData.weather[2] = sky->currentWeatherPct;
			commonData.weather[3] = sky->currentGameHour;
		}
	}

	// Update time of day
	{
		auto& settingManager = SettingManager::GetSingleton();

		// Clamp current time to valid range
		float currentTime = sky ? std::clamp(sky->currentGameHour, 0.0f, 24.0f) : 12.0f;

		// Load time of day settings using cached IDs
		const float nightTime = settingManager.GetValue<float>(ids.nightTime);
		const float sunriseTime = settingManager.GetValue<float>(ids.sunriseTime);
		const float dawnDuration = settingManager.GetValue<float>(ids.dawnDuration);
		const float dayTime = settingManager.GetValue<float>(ids.dayTime);
		const float sunsetTime = settingManager.GetValue<float>(ids.sunsetTime);
		const float duskDuration = settingManager.GetValue<float>(ids.duskDuration);

		commonData.eInteriorFactor = Util::IsInterior();

		// Initialize and set factors
		float factors[static_cast<int>(TimeOfDayFactorIndex::Count)] = { 0.0f };

		if (!commonData.eInteriorFactor) {
			// Calculate transition points
			const float dawnStart = sunriseTime - dawnDuration;
			const float dawnMid = sunriseTime - (dawnDuration * 0.5f);
			const float duskMid = sunsetTime + (duskDuration * 0.5f);
			const float duskEnd = sunsetTime + duskDuration;

			// Time points array with 24h wraparound
			const float timePoints[] = {
				nightTime, dawnStart, dawnMid, sunriseTime, dayTime, sunsetTime, duskMid, duskEnd,
				nightTime + 24.0f, dawnStart + 24.0f, dawnMid + 24.0f, sunriseTime + 24.0f,
				dayTime + 24.0f, sunsetTime + 24.0f, duskMid + 24.0f, duskEnd + 24.0f
			};

			// Find current and next time periods
			int currentIdx = 0, nextIdx = 0;
			float currentPeriodTime = 0.0f, nextPeriodTime = 24.0f;

			for (int i = 0; i < 16; i++) {
				const float t = timePoints[i];
				if (currentTime >= t && t >= currentPeriodTime) {
					currentIdx = i;
					currentPeriodTime = t;
				}
				if (t > currentTime && nextPeriodTime >= t) {
					nextIdx = i;
					nextPeriodTime = t;
				}
			}

			// Map time point indices to time of day factors
			constexpr int factorMapping[] = {
				static_cast<int>(TimeOfDayFactorIndex::Night),
				static_cast<int>(TimeOfDayFactorIndex::Night),
				static_cast<int>(TimeOfDayFactorIndex::Dawn),
				static_cast<int>(TimeOfDayFactorIndex::Sunrise),
				static_cast<int>(TimeOfDayFactorIndex::Day),
				static_cast<int>(TimeOfDayFactorIndex::Sunset),
				static_cast<int>(TimeOfDayFactorIndex::Dusk),
				static_cast<int>(TimeOfDayFactorIndex::Night)
			};
			const int currentFactor = factorMapping[currentIdx % 8];
			const int nextFactor = factorMapping[nextIdx % 8];

			// Calculate blend weight
			float timeDiff = std::abs(nextPeriodTime - currentPeriodTime);
			if (timeDiff == 0.0f)
				timeDiff = 1.0f;

			const float blend = std::abs(currentTime - currentPeriodTime) / timeDiff;

			if (currentFactor == nextFactor) {
				factors[currentFactor] = 1.0f;
			} else {
				factors[currentFactor] = std::clamp(1.0f - blend, 0.0f, 1.0f);
				factors[nextFactor] = std::clamp(blend, 0.0f, 1.0f);
			}

			constexpr float dayPowerCurve = 0.6f;
			float powDay = std::pow(factors[static_cast<int>(TimeOfDayFactorIndex::Day)], dayPowerCurve);
			powDay = std::clamp(powDay, 0.0f, 1.0f);

			if (powDay > FLT_MIN) {
				const float complement = 1.0f - powDay;

				if (factors[static_cast<int>(TimeOfDayFactorIndex::Sunrise)] > FLT_MIN) {
					factors[static_cast<int>(TimeOfDayFactorIndex::Sunrise)] = std::clamp(complement, 0.0f, 1.0f);
				}

				if (factors[static_cast<int>(TimeOfDayFactorIndex::Sunset)] > FLT_MIN) {
					factors[static_cast<int>(TimeOfDayFactorIndex::Sunset)] = std::clamp(complement, 0.0f, 1.0f);
				}
			}

			factors[static_cast<int>(TimeOfDayFactorIndex::Day)] = powDay;

			// Assign to output arrays
			commonData.timeOfDay1[static_cast<int>(TimeOfDay1Index::Dawn)] = factors[static_cast<int>(TimeOfDayFactorIndex::Dawn)];
			commonData.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunrise)] = factors[static_cast<int>(TimeOfDayFactorIndex::Sunrise)];
			commonData.timeOfDay1[static_cast<int>(TimeOfDay1Index::Day)] = factors[static_cast<int>(TimeOfDayFactorIndex::Day)];
			commonData.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunset)] = factors[static_cast<int>(TimeOfDayFactorIndex::Sunset)];
			commonData.timeOfDay2[static_cast<int>(TimeOfDay2Index::Dusk)] = factors[static_cast<int>(TimeOfDayFactorIndex::Dusk)];
			commonData.timeOfDay2[static_cast<int>(TimeOfDay2Index::Night)] = factors[static_cast<int>(TimeOfDayFactorIndex::Night)];
		}

		// Calculate distance to night time (handling 24h wraparound)
		float distToNight = std::abs(currentTime - nightTime);
		if (distToNight > 12.0f) {
			distToNight = 24.0f - distToNight;
		}

		// Calculate distance to day time (handling 24h wraparound)
		float distToDay = std::abs(currentTime - dayTime);
		if (distToDay > 12.0f) {
			distToDay = 24.0f - distToDay;
		}

		// Night/day factor: 0.0 = pure night, 1.0 = pure day
		// Based on relative proximity to day vs night times
		if (distToNight + distToDay > 0.0f) {
			commonData.eNightDayFactor = distToNight / (distToNight + distToDay);
		} else {
			commonData.eNightDayFactor = 0.5f;  // Fallback if both distances are 0
		}

		commonData.timeOfDay2[static_cast<int>(TimeOfDay2Index::InteriorDay)] = commonData.eInteriorFactor * commonData.eNightDayFactor;
		commonData.timeOfDay2[static_cast<int>(TimeOfDay2Index::InteriorNight)] = commonData.eInteriorFactor * (1.0f - commonData.eNightDayFactor);
	}
}

void EffectManager::UpdateCommonVariablesForEffect(ID3DX11Effect* effect)
{
	if (!effect)
		return;

	auto renderer = globals::game::renderer;

	// Set common textures
	Effect::SetShaderResourceVariable(effect, "TextureDepth",
		renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV);

	// Set format-specific render targets
	static const char* const formatTargets[] = {
		"RenderTargetRGBA32", "RenderTargetRGBA64", "RenderTargetRGBA64F",
		"RenderTargetR16F", "RenderTargetR32F", "RenderTargetRGB32F"
	};

	auto& textureManager = TextureManager::GetSingleton();
	for (const auto& targetName : formatTargets) {
		auto* texture = textureManager.GetCommonTexture(targetName);
		if (texture) {
			Effect::SetShaderResourceVariable(effect, targetName, texture->srv.get());
		}
	}

	// Set fixed-size render targets
	static const char* const fixedSizeTargets[] = {
		"RenderTarget1024", "RenderTarget512", "RenderTarget256", "RenderTarget128",
		"RenderTarget64", "RenderTarget32", "RenderTarget16"
	};

	for (const auto& targetName : fixedSizeTargets) {
		auto* texture = textureManager.GetCommonTexture(targetName);
		if (texture) {
			Effect::SetShaderResourceVariable(effect, targetName, texture->srv.get());
		}
	}

	// Set vector variables
	Effect::SetVectorVariable(effect, "Timer", commonData.timer, sizeof(commonData.timer));
	Effect::SetVectorVariable(effect, "Weather", commonData.weather, sizeof(commonData.weather));
	Effect::SetVectorVariable(effect, "TimeOfDay1", commonData.timeOfDay1, sizeof(commonData.timeOfDay1));
	Effect::SetVectorVariable(effect, "TimeOfDay2", commonData.timeOfDay2, sizeof(commonData.timeOfDay2));
	Effect::SetVectorVariable(effect, "ENightDayFactor", &commonData.eNightDayFactor, sizeof(commonData.eNightDayFactor));
	Effect::SetVectorVariable(effect, "EInteriorFactor", &commonData.eInteriorFactor, sizeof(commonData.eInteriorFactor));
}

void EffectManager::CopyTexture(ID3D11ShaderResourceView* a_source, ID3D11RenderTargetView* a_dest)
{
	if (!a_source || !a_dest || !copyPixelShader || !copyVertexShader) {
		logger::critical("[ENBPP] Invalid parameters or shaders not initialized for texture copy");
		return;
	}

	auto context = globals::d3d::context;

	// Set viewport based on destination render target
	winrt::com_ptr<ID3D11Resource> resource;
	a_dest->GetResource(resource.put());
	winrt::com_ptr<ID3D11Texture2D> texture;
	if (!resource || !resource.try_as(texture) || !texture) {
		logger::error("[ENBPP] Failed to get Texture2D from destination render target");
		return;
	}
	D3D11_TEXTURE2D_DESC texDesc;
	texture->GetDesc(&texDesc);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(texDesc.Width);
	viewport.Height = static_cast<float>(texDesc.Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set up for copy operation
	context->OMSetRenderTargets(1, &a_dest, nullptr);
	context->OMSetDepthStencilState(nullptr, 0);
	context->RSSetState(rasterizerState.get());
	context->OMSetBlendState(blendState.get(), nullptr, 0xFFFFFFFF);

	// Set IA state
	UINT stride = 20;  // 3 floats position + 2 floats texcoord
	UINT offset = 0;
	ID3D11Buffer* vbs[] = { quadVertexBuffer.get() };
	context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
	context->IASetInputLayout(inputLayout.get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// Set shaders
	context->VSSetShader(copyVertexShader.get(), nullptr, 0);
	context->PSSetShader(copyPixelShader.get(), nullptr, 0);

	// Set source texture
	context->PSSetShaderResources(0, 1, &a_source);

	// Draw fullscreen quad
	context->Draw(4, 0);

	// Clean up SRV binding
	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(0, 1, &nullSRV);
}

void EffectManager::ApplyColorCorrection(ID3D11UnorderedAccessView* textureUAV)
{
	if (!textureUAV || !colorCorrectionComputeShader || !colorCorrectionConstantBuffer) {
		logger::warn("[ENBPP] Invalid parameters or shaders not initialized for color correction");
		return;
	}

	auto& settingManager = SettingManager::GetSingleton();

	auto brightness = settingManager.GetValue<float>(ids.brightness);
	auto gammaCurve = settingManager.GetValue<float>(ids.gammaCurve);

	if (brightness == 1.0f && gammaCurve == 1.0f)
		return;

	auto context = globals::d3d::context;

	// Update constant buffer with current settings
	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = context->Map(colorCorrectionConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr)) {
		logger::warn("[ENBPP] Failed to map color correction constant buffer");
		return;
	}
	{
		float* cbData = static_cast<float*>(mapped.pData);
		cbData[0] = brightness;
		cbData[1] = gammaCurve;
		context->Unmap(colorCorrectionConstantBuffer.get(), 0);
	}

	// Set compute shader and resources
	context->CSSetShader(colorCorrectionComputeShader.get(), nullptr, 0);
	ID3D11Buffer* bufferArray[] = { colorCorrectionConstantBuffer.get() };
	context->CSSetConstantBuffers(0, 1, bufferArray);
	context->CSSetUnorderedAccessViews(0, 1, &textureUAV, nullptr);

	// Get texture dimensions for dispatch
	winrt::com_ptr<ID3D11Resource> resource;
	textureUAV->GetResource(resource.put());
	winrt::com_ptr<ID3D11Texture2D> texture;
	if (!resource || !resource.try_as(texture) || !texture) {
		logger::error("[ENBPP] Failed to get Texture2D from UAV in ApplyColorCorrection");
	} else {
		D3D11_TEXTURE2D_DESC texDesc;
		texture->GetDesc(&texDesc);

		// Dispatch compute shader (8x8 thread groups)
		UINT dispatchX = (texDesc.Width + 7) / 8;
		UINT dispatchY = (texDesc.Height + 7) / 8;
		context->Dispatch(dispatchX, dispatchY, 1);
	}

	// Clear bindings
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetShader(nullptr, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &nullCB);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void EffectManager::RenderEffectsList()
{
	Effect* effects[] = { &enbBloom, &enbAdaptation, &enbEffect, &enbEffectPostPass };

	struct VarRef
	{
		Effect* effect;
		int index;
	};

	auto computeUniqueName = [](const Effect::UIVariable& var) -> std::string {
		if (!var.uniqueName.empty())
			return var.uniqueName;
		if (!var.group.empty())
			return var.group + "." + var.displayName;
		return var.displayName;
	};

	std::unordered_map<std::string, VarRef> uniqueNameMap;
	for (auto* effect : effects) {
		if (!effect->IsCompiled())
			continue;
		for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
			auto& var = effect->uiVariables[i];
			if (var.isSeparator)
				continue;
			uniqueNameMap[computeUniqueName(var)] = { effect, i };
		}
	}

	auto evaluateBinding = [&](const Effect::UIVariable& var) -> std::pair<bool, bool> {
		bool visible = true;
		bool readOnly = var.isReadOnly;
		if (var.uiBinding.empty())
			return { visible, readOnly };

		auto it = uniqueNameMap.find(var.uiBinding);
		if (it == uniqueNameMap.end())
			return { visible, readOnly };

		const auto& boundVar = it->second.effect->uiVariables[it->second.index];
		float boundValue = 0.0f;
		switch (boundVar.type) {
		case Effect::UIVariableType::Float:
			boundValue = boundVar.floatValue;
			break;
		case Effect::UIVariableType::Int:
			boundValue = static_cast<float>(boundVar.intValue);
			break;
		case Effect::UIVariableType::Bool:
			boundValue = boundVar.boolValue ? 1.0f : 0.0f;
			break;
		default:
			break;
		}

		bool conditionMet = false;
		if (var.uiBindingCondition.empty()) {
			conditionMet = (boundValue != 0.0f);
		} else {
			std::string cond = var.uiBindingCondition;
			std::string op;
			float comparand = 0.0f;

			if (cond.size() >= 2 && (cond.substr(0, 2) == "==" || cond.substr(0, 2) == "!=" ||
										cond.substr(0, 2) == "<=" || cond.substr(0, 2) == ">=" ||
										cond.substr(0, 2) == "=<" || cond.substr(0, 2) == "=>")) {
				op = cond.substr(0, 2);
				try {
					comparand = std::stof(cond.substr(2));
				} catch (...) {
				}
			} else if (!cond.empty() && (cond[0] == '<' || cond[0] == '>')) {
				op = cond.substr(0, 1);
				try {
					comparand = std::stof(cond.substr(1));
				} catch (...) {
				}
			}

			if (op == "==")
				conditionMet = (boundValue == comparand);
			else if (op == "!=")
				conditionMet = (boundValue != comparand);
			else if (op == "<")
				conditionMet = (boundValue < comparand);
			else if (op == ">")
				conditionMet = (boundValue > comparand);
			else if (op == "<=" || op == "=<")
				conditionMet = (boundValue <= comparand);
			else if (op == ">=" || op == "=>")
				conditionMet = (boundValue >= comparand);
		}

		std::string prop = var.uiBindingProperty;
		std::transform(prop.begin(), prop.end(), prop.begin(), ::tolower);

		if (prop == "hidden")
			visible = !conditionMet;
		else if (prop == "visible")
			visible = conditionMet;
		else if (prop == "readonly")
			readOnly = conditionMet;
		else if (prop == "readwrite")
			readOnly = !conditionMet;

		return { visible, readOnly };
	};

	struct GroupNode
	{
		std::string name;
		std::string fullPath;
		std::vector<VarRef> vars;
		std::vector<std::unique_ptr<GroupNode>> children;
	};
	GroupNode root;

	std::unordered_map<std::string, std::string> mergedGroupDisplayNames;
	std::unordered_map<std::string, bool> mergedGroupDefaultOpen;
	std::unordered_map<std::string, int> mergedGroupOrdering;

	for (auto* effect : effects) {
		if (!effect->IsCompiled())
			continue;

		for (auto& [path, name] : effect->groupDisplayNames) {
			if (mergedGroupDisplayNames.find(path) == mergedGroupDisplayNames.end())
				mergedGroupDisplayNames[path] = name;
		}
		for (auto& [path, open] : effect->groupDefaultOpen) {
			if (mergedGroupDefaultOpen.find(path) == mergedGroupDefaultOpen.end())
				mergedGroupDefaultOpen[path] = open;
		}
		for (auto& [path, order] : effect->groupOrdering) {
			if (mergedGroupOrdering.find(path) == mergedGroupOrdering.end())
				mergedGroupOrdering[path] = order;
		}

		for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
			auto& var = effect->uiVariables[i];
			GroupNode* node = &root;

			if (!var.isTopLevel && !var.group.empty()) {
				std::istringstream ss(var.group);
				std::string segment;
				std::string builtPath;
				while (std::getline(ss, segment, '.')) {
					if (!builtPath.empty())
						builtPath += ".";
					builtPath += segment;

					GroupNode* child = nullptr;
					for (auto& c : node->children) {
						if (c->name == segment) {
							child = c.get();
							break;
						}
					}
					if (!child) {
						auto nc = std::make_unique<GroupNode>();
						nc->name = segment;
						nc->fullPath = builtPath;
						child = nc.get();
						node->children.push_back(std::move(nc));
					}
					node = child;
				}
			}

			if (node == &root) {
				if (var.isSeparator)
					continue;
				bool isDuplicate = false;
				for (auto& existing : root.vars) {
					auto& ev = existing.effect->uiVariables[existing.index];
					if (!var.displayName.empty() && ev.displayName == var.displayName) {
						isDuplicate = true;
						break;
					}
				}
				if (isDuplicate)
					continue;
			}

			node->vars.push_back({ effect, i });
		}
	}

	std::function<void(GroupNode&)> sortNode = [&](GroupNode& node) {
		// Sort by UIOrdering (descending), then sourceOrder (ascending) as tiebreaker
		std::stable_sort(node.vars.begin(), node.vars.end(), [](const VarRef& a, const VarRef& b) {
			auto& va = a.effect->uiVariables[a.index];
			auto& vb = b.effect->uiVariables[b.index];
			if (va.ordering != vb.ordering)
				return va.ordering > vb.ordering;
			if (va.sourceOrder != vb.sourceOrder)
				return va.sourceOrder < vb.sourceOrder;
			return false;
		});
		std::stable_sort(node.children.begin(), node.children.end(), [&](const auto& a, const auto& b) {
			int oA = 0, oB = 0;
			auto itA = mergedGroupOrdering.find(a->fullPath);
			if (itA != mergedGroupOrdering.end())
				oA = itA->second;
			auto itB = mergedGroupOrdering.find(b->fullPath);
			if (itB != mergedGroupOrdering.end())
				oB = itB->second;
			return oA > oB;
		});
		for (auto& child : node.children)
			sortNode(*child);
	};
	sortNode(root);

	std::unordered_set<Effect*> changedEffects;

	struct TechDropdown
	{
		Effect* effect;
		std::string group;
		bool topLevel;
		int ordering;
	};
	std::vector<TechDropdown> techDropdowns;
	for (auto* effect : effects) {
		if (!effect->IsCompiled())
			continue;
		if (effect->uiTechniques.size() > 1 && effect->techniqueDropdownVisible) {
			techDropdowns.push_back({ effect, effect->techniqueDropdownGroup, effect->techniqueDropdownTopLevel, effect->techniqueDropdownOrdering });
		}
	}

	std::stable_sort(techDropdowns.begin(), techDropdowns.end(), [](const TechDropdown& a, const TechDropdown& b) {
		return a.ordering > b.ordering;
	});

	auto renderTechDropdown = [&](TechDropdown& td) {
		ImGui::Text("%s", td.effect->techniqueDropdownName.c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		const char* current = td.effect->uiTechniques[td.effect->selectedTechniqueIndex].displayName.c_str();
		if (ImGui::BeginCombo(("##TECHNIQUE_" + td.effect->GetName()).c_str(), current)) {
			for (uint32_t i = 0; i < td.effect->uiTechniques.size(); ++i) {
				if (ImGui::Selectable(td.effect->uiTechniques[i].displayName.c_str(), td.effect->selectedTechniqueIndex == i)) {
					td.effect->selectedTechniqueIndex = i;
					changedEffects.insert(td.effect);
				}
				if (td.effect->selectedTechniqueIndex == i)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	};

	for (auto& td : techDropdowns) {
		if (td.topLevel || td.group.empty())
			renderTechDropdown(td);
	}

	auto renderWidget = [&](const std::string& label, const std::string& id, Effect::UIVariable& uiVar,
							Effect::UIVariableType type, Effect::UIWidgetType widget,
							float* floatVal, int* intVal, bool* boolVal, float* colorVal,
							bool readOnly, Effect* effect) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		if (readOnly)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

		ImGui::Text("%s", label.c_str());

		bool isLabelOnly = ((type == Effect::UIVariableType::Float && uiVar.floatMin == 0 && uiVar.floatMax == 0) ||
							(type == Effect::UIVariableType::Int && uiVar.intMin == 0 && uiVar.intMax == 0));

		if (!isLabelOnly) {
			ImGui::TableSetColumnIndex(1);

			if (readOnly)
				ImGui::BeginDisabled();

			bool changed = false;
			switch (type) {
			case Effect::UIVariableType::Float:
				changed = ImGui::SliderFloat(id.c_str(), floatVal, uiVar.floatMin, uiVar.floatMax, "%.3f");
				break;
			case Effect::UIVariableType::Int:
				if ((widget == Effect::UIWidgetType::Dropdown || widget == Effect::UIWidgetType::Quality) && !uiVar.dropdownItems.empty()) {
					int displayIndex = *intVal;
					if (widget == Effect::UIWidgetType::Quality)
						displayIndex = *intVal + 1;
					const char* currentItem = (displayIndex >= 0 && displayIndex < static_cast<int>(uiVar.dropdownItems.size())) ? uiVar.dropdownItems[displayIndex].c_str() : "";
					if (ImGui::BeginCombo(id.c_str(), currentItem)) {
						for (int j = 0; j < static_cast<int>(uiVar.dropdownItems.size()); ++j) {
							int itemValue = (widget == Effect::UIWidgetType::Quality) ? (j - 1) : j;
							if (ImGui::Selectable(uiVar.dropdownItems[j].c_str(), *intVal == itemValue)) {
								*intVal = itemValue;
								changed = true;
							}
						}
						ImGui::EndCombo();
					}
				} else {
					changed = ImGui::SliderInt(id.c_str(), intVal, uiVar.intMin, uiVar.intMax);
				}
				break;
			case Effect::UIVariableType::Bool:
				changed = ImGui::Checkbox(id.c_str(), boolVal);
				break;
			case Effect::UIVariableType::Color3:
				if (widget == Effect::UIWidgetType::Vector)
					changed = ImGui::SliderFloat3(id.c_str(), colorVal, -1.0f, 1.0f, "%.3f");
				else
					changed = ImGui::ColorEdit3(id.c_str(), colorVal);
				break;
			case Effect::UIVariableType::Color4:
				changed = ImGui::ColorEdit4(id.c_str(), colorVal);
				break;
			}

			if (changed)
				changedEffects.insert(effect);

			if (readOnly)
				ImGui::EndDisabled();
		}

		if (readOnly)
			ImGui::PopStyleColor();
	};

	auto isLabelOnly = [](const Effect::UIVariable& v) {
		if (v.isSeparator)
			return false;
		return (v.type == Effect::UIVariableType::Float && v.floatMin == 0.0f && v.floatMax == 0.0f) ||
		       (v.type == Effect::UIVariableType::Int && v.intMin == 0 && v.intMax == 0);
	};

	int tableCounter = 0;

	auto beginVarTable = [&]() {
		std::string tableId = "##ut_" + std::to_string(tableCounter++);
		if (ImGui::BeginTable(tableId.c_str(), 2, ImGuiTableFlags_SizingFixedFit)) {
			float availWidth = ImGui::GetContentRegionAvail().x;
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, availWidth * 0.45f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, availWidth * 0.55f);
			return true;
		}
		return false;
	};

	auto renderVar = [&](VarRef& ref, bool& inTable) {
		auto& uiVar = ref.effect->uiVariables[ref.index];

		if (uiVar.isSeparator) {
			if (inTable) {
				ImGui::EndTable();
				inTable = false;
			}
			ImGui::Separator();
			return;
		}

		if (uiVar.displayName.empty() || uiVar.isHidden)
			return;

		auto [bindVisible, bindReadOnly] = evaluateBinding(uiVar);
		if (!bindVisible)
			return;

		if (isLabelOnly(uiVar)) {
			if (inTable) {
				ImGui::EndTable();
				inTable = false;
			}
			if (uiVar.isReadOnly)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::TextWrapped("%s", uiVar.displayName.c_str());
			if (uiVar.isReadOnly)
				ImGui::PopStyleColor();
		} else {
			if (!inTable) {
				if (!beginVarTable())
					return;
				inTable = true;
			}
			std::string baseId = "##uv_" + std::to_string(ref.index) + "_" + ref.effect->GetName();
			renderWidget(uiVar.displayName, baseId, uiVar, uiVar.type, uiVar.widgetType,
				&uiVar.floatValue, &uiVar.intValue, &uiVar.boolValue, uiVar.colorValue,
				bindReadOnly, ref.effect);
		}
	};

	auto renderChildNode = [&](GroupNode& child) {
		std::string displayName = child.name;
		auto nameIt = mergedGroupDisplayNames.find(child.fullPath);
		if (nameIt != mergedGroupDisplayNames.end())
			displayName = nameIt->second;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
		auto openIt = mergedGroupDefaultOpen.find(child.fullPath);
		if (openIt == mergedGroupDefaultOpen.end() || openIt->second)
			flags |= ImGuiTreeNodeFlags_DefaultOpen;

		std::string nodeLabel = displayName + "###ugrp_" + child.fullPath;
		return ImGui::TreeNodeEx(nodeLabel.c_str(), flags);
	};

	// Collect root-level separators and map each to the child group it precedes
	std::vector<VarRef> rootSeparators;
	for (auto* effect : effects) {
		if (!effect->IsCompiled())
			continue;
		for (int i = 0; i < static_cast<int>(effect->uiVariables.size()); ++i) {
			auto& var = effect->uiVariables[i];
			if (var.isSeparator && var.group.empty())
				rootSeparators.push_back({ effect, i });
		}
	}

	// For each child, find separators that should render before it
	std::unordered_map<std::string, std::vector<int>> separatorsBeforeGroup;

	auto getMinSourceOrderForEffect = [](GroupNode& node, Effect* effect, auto& self) -> int {
		int minOrder = INT_MAX;
		for (auto& ref : node.vars) {
			if (ref.effect == effect && ref.effect->uiVariables[ref.index].sourceOrder < minOrder)
				minOrder = ref.effect->uiVariables[ref.index].sourceOrder;
		}
		for (auto& child : node.children) {
			int childOrder = self(*child, effect, self);
			if (childOrder < minOrder)
				minOrder = childOrder;
		}
		return minOrder;
	};

	for (size_t si = 0; si < rootSeparators.size(); ++si) {
		auto& sep = rootSeparators[si];
		auto& sepVar = sep.effect->uiVariables[sep.index];
		int bestChildIdx = -1;
		int bestMinSO = INT_MAX;
		for (size_t ci = 0; ci < root.children.size(); ++ci) {
			int minSO = getMinSourceOrderForEffect(*root.children[ci], sep.effect, getMinSourceOrderForEffect);
			if (minSO > sepVar.sourceOrder && minSO < bestMinSO) {
				bestMinSO = minSO;
				bestChildIdx = static_cast<int>(ci);
			}
		}
		if (bestChildIdx >= 0)
			separatorsBeforeGroup[root.children[bestChildIdx]->fullPath].push_back(static_cast<int>(si));
	}

	std::function<void(GroupNode&)> renderGroup = [&](GroupNode& node) {
		for (auto& td : techDropdowns) {
			if (!td.topLevel && !td.group.empty() && td.group == node.fullPath)
				renderTechDropdown(td);
		}

		if (!node.vars.empty()) {
			bool inTable = false;
			for (auto& ref : node.vars)
				renderVar(ref, inTable);
			if (inTable)
				ImGui::EndTable();
		}

		for (auto& child : node.children) {
			auto sepIt = separatorsBeforeGroup.find(child->fullPath);
			if (sepIt != separatorsBeforeGroup.end()) {
				ImGui::Separator();
			}
			if (renderChildNode(*child)) {
				renderGroup(*child);
				ImGui::TreePop();
			}
		}
	};

	renderGroup(root);

	for (auto* effect : changedEffects)
		effect->UpdateUIVariables();

	if (enbLens.IsCompiled()) {
		ImGui::Separator();
		if (ImGui::TreeNodeEx("enblens.fx", ImGuiTreeNodeFlags_DefaultOpen)) {
			enbLens.RenderImGui();
			ImGui::TreePop();
		}
	}

	for (auto* effect : effects) {
		if (!effect->GetErrors().empty()) {
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s:", effect->GetName().c_str());
			for (const auto& err : effect->GetErrors())
				ImGui::TextWrapped("%s", err.c_str());
		}
	}
	if (!enbLens.GetErrors().empty()) {
		ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s:", enbLens.GetName().c_str());
		for (const auto& err : enbLens.GetErrors())
			ImGui::TextWrapped("%s", err.c_str());
	}
}