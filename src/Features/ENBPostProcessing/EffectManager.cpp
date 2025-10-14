#include "EffectManager.h"

#include "State.h"

#include "SettingManager.h"
#include "TextureManager.h"
#include "WeatherManager.h"

#include <d3dcompiler.h>
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
	CreateCommonResources();
	Apply();
}

void EffectManager::Apply()
{
	enbDepthOfField.Apply();
	enbBloom.Apply();
	enbLens.Apply();
	enbAdaptation.Apply();
	enbEffect.Apply();
	enbEffectPostPass.Apply();
}

void EffectManager::Load()
{
	enbDepthOfField.Load();
	enbBloom.Load();
	enbLens.Load();
	enbAdaptation.Load();
	enbEffect.Load();
	enbEffectPostPass.Load();
}

void EffectManager::Save()
{
	enbDepthOfField.Save();
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
	settingManager.RegisterFloatSetting("DawnDuration", "TIMEOFDAY", 1.6f, 0.1f, 12.0f, false);
	settingManager.RegisterFloatSetting("SunriseTime", "TIMEOFDAY", 9.0f, 0.0f, 24.0f, false);
	settingManager.RegisterFloatSetting("DayTime", "TIMEOFDAY", 12.0f, 0.0f, 24.0f, false);
	settingManager.RegisterFloatSetting("SunsetTime", "TIMEOFDAY", 17.25f, 0.0f, 24.0f, false);
	settingManager.RegisterFloatSetting("DuskDuration", "TIMEOFDAY", 2.0f, 0.1f, 12.0f, false);
	settingManager.RegisterFloatSetting("NightTime", "TIMEOFDAY", 1.0f, 0.0f, 24.0f, false);

	// EFFECT
	settingManager.RegisterBoolSetting("UseOriginalPostProcessing", "EFFECT", false, false);

	settingManager.RegisterBoolSetting("EnablePostPassShader", "EFFECT", false, false);
	settingManager.RegisterBoolSetting("EnableAdaptation", "EFFECT", true, false);
	settingManager.RegisterBoolSetting("EnableBloom", "EFFECT", true, false);
	settingManager.RegisterBoolSetting("EnableLens", "EFFECT", false, false);
	settingManager.RegisterBoolSetting("EnableDepthOfField", "EFFECT", true, false);

	settingManager.RegisterFloatSetting("Brightness", "COLORCORRECTION", 1.0f, 0.0f, 3.0f, false);
	settingManager.RegisterFloatSetting("GammaCurve", "COLORCORRECTION", 1.0f, 0.1f, 3.0f, false);

	settingManager.RegisterFloatSetting("AdaptationSensitivity", "ADAPTATION", 1.0f, 0.0f, 5.0f, false);
	settingManager.RegisterBoolSetting("ForceMinMaxValues", "ADAPTATION", false, false);
	settingManager.RegisterFloatSetting("AdaptationMin", "ADAPTATION", 0.0f, 0.0f, 1.0f, false);
	settingManager.RegisterFloatSetting("AdaptationMax", "ADAPTATION", 1.0f, 0.0f, 2.0f, false);
	settingManager.RegisterFloatSetting("AdaptationTime", "ADAPTATION", 1.0f, 0.1f, 10.0f, false);

	settingManager.RegisterFloatSetting("FocusingTime", "DEPTHOFFIELD", 1.0f, 0.1f, 10.0f, false);
	settingManager.RegisterFloatSetting("ApertureTime", "DEPTHOFFIELD", 1.0f, 0.1f, 10.0f, false);

	settingManager.RegisterTimeOfDaySetting("Amount", "BLOOM", 1, true);
	settingManager.RegisterTimeOfDaySetting("Amount", "LENS", 1, true);

	// SKY
	settingManager.RegisterTimeOfDaySetting("GradientIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("GradientDesaturation", "SKY", 0, true);

	settingManager.RegisterTimeOfDaySetting("GradientTopIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("GradientTopCurve", "SKY", 1, true);
	settingManager.RegisterColorTimeOfDaySetting("GradientTopColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("GradientMiddleIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("GradientMiddleCurve", "SKY", 1, true);
	settingManager.RegisterColorTimeOfDaySetting("GradientMiddleColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("GradientHorizonIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("GradientHorizonCurve", "SKY", 1, true);
	settingManager.RegisterColorTimeOfDaySetting("GradientHorizonColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("CloudsIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("CloudsCurve", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("CloudsDesaturation", "SKY", 0, true);
	settingManager.RegisterTimeOfDaySetting("CloudsOpacity", "SKY", 1, true);
	settingManager.RegisterColorTimeOfDaySetting("CloudsColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("SunIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("SunDesaturation", "SKY", 0, true);
	settingManager.RegisterColorTimeOfDaySetting("SunColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("MoonIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("MoonDesaturation", "SKY", 0, true);
	settingManager.RegisterColorTimeOfDaySetting("MoonColorFilter", "SKY", { 1.0f, 1.0f, 1.0f }, true);

	settingManager.RegisterTimeOfDaySetting("StarsIntensity", "SKY", 1, true);
	settingManager.RegisterTimeOfDaySetting("StarsCurve", "SKY", 1, true);

	settingManager.RegisterFloatSetting("CloudsEdgeClamp", "SKY", 0.0f);
	settingManager.RegisterFloatSetting("CloudsEdgeIntensity", "SKY", 0.0f);
	settingManager.RegisterFloatSetting("CloudsEdgeFadeRange", "SKY", 0.0f);
	settingManager.RegisterFloatSetting("CloudsEdgeMoonMultiplier", "SKY", 0.0f);

	// ENVIRONMENT
	settingManager.RegisterTimeOfDaySetting("DirectLightingIntensity", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("DirectLightingCurve", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("DirectLightingDesaturation", "ENVIRONMENT", 0, true);

	settingManager.RegisterTimeOfDaySetting("AmbientLightingIntensity", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("AmbientLightingDesaturation", "ENVIRONMENT", 0, true);

	settingManager.RegisterColorTimeOfDaySetting("DirectLightingColorFilter", "ENVIRONMENT", { 1.0f, 1.0f, 1.0f }, true);
	settingManager.RegisterTimeOfDaySetting("DirectLightingColorFilterAmount", "ENVIRONMENT", 0, true);

	settingManager.RegisterTimeOfDaySetting("FogColorMultiplier", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("FogColorCurve", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("FogAmountMultiplier", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("FogCurveMultiplier", "ENVIRONMENT", 1, true);
	settingManager.RegisterTimeOfDaySetting("FogColorMultiplier", "ENVIRONMENT", 1, true);
	settingManager.RegisterColorTimeOfDaySetting("FogColorFilter", "ENVIRONMENT", { 1.0f, 1.0f, 1.0f }, true);
	settingManager.RegisterTimeOfDaySetting("FogColorFilterAmount", "ENVIRONMENT", 0, true);

	settingManager.RegisterTimeOfDaySetting("ColorPow", "ENVIRONMENT", 1, true);

	// IMAGEBASEDLIGHTING
	settingManager.RegisterTimeOfDaySetting("AdditiveAmount", "IMAGEBASEDLIGHTING", 0, true);
	settingManager.RegisterTimeOfDaySetting("MultiplicativeAmount", "IMAGEBASEDLIGHTING", 0, true);
	settingManager.RegisterTimeOfDaySetting("ReflectiveAmount", "IMAGEBASEDLIGHTING", 0, true);

	// SUNGLARE
	settingManager.RegisterTimeOfDaySetting("GlowIntensity", "SUNGLARE", 1, true);

	// VOLUMETRICFOG
	settingManager.RegisterTimeOfDaySetting("Intensity", "VOLUMETRICFOG", 1, true);
	settingManager.RegisterTimeOfDaySetting("Curve", "VOLUMETRICFOG", 1, true);
	settingManager.RegisterTimeOfDaySetting("Opacity", "VOLUMETRICFOG", 1, true);
	settingManager.RegisterColorTimeOfDaySetting("ColorFilter", "VOLUMETRICFOG", { 1.0f, 1.0f, 1.0f }, true);

	// GAMEVOLUMETRICRAYS
	settingManager.RegisterTimeOfDaySetting("Intensity", "GAMEVOLUMETRICRAYS", 1, true);
	settingManager.RegisterTimeOfDaySetting("RangeFactor", "GAMEVOLUMETRICRAYS", 1, true);
	settingManager.RegisterTimeOfDaySetting("Desaturation", "GAMEVOLUMETRICRAYS", 0, true);
	settingManager.RegisterColorTimeOfDaySetting("ColorFilter", "GAMEVOLUMETRICRAYS", { 1.0f, 1.0f, 1.0f }, true);
}

void EffectManager::ExecuteEffects()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

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

	auto state = globals::state;

	auto& settingManager = SettingManager::GetSingleton();
	auto& textureManager = TextureManager::GetSingleton();

	if (enbDepthOfField.IsCompiled() && settingManager.GetValue<bool>("EnableDepthOfField", "EFFECT")) {
		state->BeginPerfEvent(enbDepthOfField.GetName());
		UpdateCommonVariablesForEffect(enbDepthOfField.GetEffect());
		enbDepthOfField.UpdateEffectVariables();
		enbDepthOfField.Execute();
		state->EndPerfEvent();
	}

	// Downsampled texture shared between bloom, lens and adaptation
	textureManager.UpdateDownsampledTexture(textureOriginal.SRV);

	if (enbBloom.IsCompiled() && settingManager.GetValue<bool>("EnableBloom", "EFFECT")) {
		state->BeginPerfEvent(enbBloom.GetName());
		UpdateCommonVariablesForEffect(enbBloom.GetEffect());
		enbBloom.UpdateEffectVariables();
		enbBloom.Execute();
		state->EndPerfEvent();
	}

	if (enbLens.IsCompiled() && settingManager.GetValue<bool>("EnableLens", "EFFECT")) {
		state->BeginPerfEvent(enbLens.GetName());
		UpdateCommonVariablesForEffect(enbLens.GetEffect());
		enbLens.UpdateEffectVariables();
		enbLens.Execute();
		state->EndPerfEvent();
	}

	if (enbAdaptation.IsCompiled() && settingManager.GetValue<bool>("EnableAdaptation", "EFFECT")) {
		state->BeginPerfEvent(enbAdaptation.GetName());
		UpdateCommonVariablesForEffect(enbAdaptation.GetEffect());
		enbAdaptation.UpdateEffectVariables();
		enbAdaptation.Execute();
		state->EndPerfEvent();
	}

	if (enbEffect.IsCompiled()) {
		state->BeginPerfEvent(enbEffect.GetName());
		UpdateCommonVariablesForEffect(enbEffect.GetEffect());
		enbEffect.UpdateEffectVariables();
		enbEffect.Execute();
		state->EndPerfEvent();
	}

	if (enbEffectPostPass.IsCompiled() && settingManager.GetValue<bool>("EnablePostPassShader", "EFFECT")) {
		state->BeginPerfEvent(enbEffectPostPass.GetName());
		UpdateCommonVariablesForEffect(enbEffectPostPass.GetEffect());
		enbEffectPostPass.UpdateEffectVariables();
		enbEffectPostPass.Execute();
		state->EndPerfEvent();
	}

	textureManager.IncrementTextureSwap();

	// Copy final render target to framebuffers
	auto textureSDRTemp = TextureManager::GetSingleton().GetCommonTexture("TextureSDRTemp");
	auto textureFramebuffer1 = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
	auto textureFramebuffer2 = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
	auto textureFramebuffer3 = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2];

	CopyTexture(textureSDRTemp->srv.get(), textureFramebuffer1.RTV);
	CopyTexture(textureSDRTemp->srv.get(), textureFramebuffer2.RTV);
	CopyTexture(textureSDRTemp->srv.get(), textureFramebuffer3.RTV);
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

	// Create a simple vertex shader for the input layout
	winrt::com_ptr<ID3DBlob> vertexShaderBlob;
	const char* vertexShaderSource = R"(
        struct VS_INPUT_POST { float3 pos : POSITION; float2 txcoord : TEXCOORD0; };
        struct VS_OUTPUT_POST { float4 pos : SV_POSITION; float2 txcoord0 : TEXCOORD0; };
        VS_OUTPUT_POST VS_Draw(VS_INPUT_POST IN) {
            VS_OUTPUT_POST OUT;
            OUT.pos = float4(IN.pos, 1.0);
            OUT.txcoord0 = IN.txcoord;
            return OUT;
        }
    )";

	winrt::com_ptr<ID3DBlob> errorBlob;
	HRESULT hr = D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr, nullptr,
		"VS_Draw", "vs_4_0", 0, 0, vertexShaderBlob.put(), errorBlob.put());

	if (SUCCEEDED(hr)) {
		hr = globals::d3d::device->CreateInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs),
			vertexShaderBlob->GetBufferPointer(),
			vertexShaderBlob->GetBufferSize(),
			inputLayout.put());
		if (FAILED(hr)) {
			logger::error("[ENBPP] Failed to create shared input layout for ENB effects");
		}
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
	// Compile vertex shader for texture copy
	const char* vertexShaderSource = R"(
		struct VS_INPUT { float3 pos : POSITION; float2 txcoord : TEXCOORD0; };
		struct VS_OUTPUT { float4 pos : SV_POSITION; float2 txcoord0 : TEXCOORD0; };

		VS_OUTPUT main(VS_INPUT input) {
			VS_OUTPUT output;
			output.pos = float4(input.pos, 1.0);
			output.txcoord0 = input.txcoord;
			return output;
		}
	)";

	winrt::com_ptr<ID3DBlob> vsBlob, errorBlob;
	HRESULT hr = D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr, nullptr,
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

	// Compile pixel shader for texture copy
	const char* pixelShaderSource = R"(
		Texture2D SourceTexture : register(t0);

		struct PS_INPUT { float4 pos : SV_POSITION; float2 txcoord0 : TEXCOORD0; };

		float4 main(PS_INPUT input) : SV_TARGET {
			return SourceTexture.Load(int3(input.pos.xy, 0));
		}
	)";

	winrt::com_ptr<ID3DBlob> psBlob;
	hr = D3DCompile(pixelShaderSource, strlen(pixelShaderSource), nullptr, nullptr, nullptr,
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
	// Compile compute shader for color correction
	const char* computeShaderSource = R"(
		cbuffer ColorCorrectionParams : register(b0)
		{
			float Brightness;
			float GammaCurve;
		};

		RWTexture2D<float4> OutputTexture : register(u0);

		[numthreads(8, 8, 1)]
		void main(uint3 id : SV_DispatchThreadID)
		{
			float4 color = OutputTexture[id.xy];
			color.rgb = pow(color.rgb, GammaCurve);
			color.rgb *= Brightness;
			OutputTexture[id.xy] = max(0, color);
		}
	)";

	winrt::com_ptr<ID3DBlob> csBlob, errorBlob;
	HRESULT hr = D3DCompile(computeShaderSource, strlen(computeShaderSource), nullptr, nullptr, nullptr,
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
	auto state = globals::state;
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

	// Update screen size
	{
		float aspect = state->screenSize.x / state->screenSize.y;

		commonData.screenSize[0] = state->screenSize.x;
		commonData.screenSize[1] = 1.0f / state->screenSize.x;
		commonData.screenSize[2] = aspect;
		commonData.screenSize[3] = 1.0f / aspect;
	}

	// Update weather
	{
		// Strip plugin index (2 leftmost digits) from form IDs
		auto stripPluginIndex = [](uint32_t formID) -> uint32_t {
			return formID & 0x00FFFFFF;  // Keep only the lower 6 hex digits
		};

		if (sky) {
			commonData.weather[0] = sky->currentWeather ? static_cast<float>(stripPluginIndex(sky->currentWeather->formID)) : 0;
			commonData.weather[1] = sky->lastWeather ? static_cast<float>(stripPluginIndex(sky->lastWeather->formID)) : 0;
			commonData.weather[2] = sky->currentWeatherPct;
			commonData.weather[3] = sky->currentGameHour;
		}
	}

	// Update time of day
	{
		auto& settingManager = SettingManager::GetSingleton();

		// Clamp current time to valid range
		float currentTime = sky ? std::clamp(sky->currentGameHour, 0.0f, 24.0f) : 12.0f;

		// Load time of day settings
		const float nightTime = settingManager.GetValue<float>("NightTime", "TIMEOFDAY");
		const float sunriseTime = settingManager.GetValue<float>("SunriseTime", "TIMEOFDAY");
		const float dawnDuration = settingManager.GetValue<float>("DawnDuration", "TIMEOFDAY");
		const float dayTime = settingManager.GetValue<float>("DayTime", "TIMEOFDAY");
		const float sunsetTime = settingManager.GetValue<float>("SunsetTime", "TIMEOFDAY");
		const float duskDuration = settingManager.GetValue<float>("DuskDuration", "TIMEOFDAY");

		commonData.eInteriorFactor = Util::IsInterior();

		// Initialize and set factors
		float factors[6] = { 0.0f };  // dawn, sunrise, day, sunset, dusk, night

		if (!commonData.eInteriorFactor) {
			// Calculate transition points
			const float dawnStart = sunriseTime - dawnDuration;
			const float dawnMid = sunriseTime - (dawnDuration * 0.5f);
			const float sunsetMid = sunsetTime + (duskDuration * 0.5f);
			const float duskEnd = sunsetTime + duskDuration;

			// Time points array with 24h wraparound
			const float timePoints[] = {
				nightTime, dawnStart, dawnMid, sunriseTime, dayTime, sunsetTime, sunsetMid, duskEnd,
				nightTime + 24.0f, dawnStart + 24.0f, dawnMid + 24.0f, sunriseTime + 24.0f,
				dayTime + 24.0f, sunsetTime + 24.0f, sunsetMid + 24.0f, duskEnd + 24.0f
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
			constexpr int factorMapping[] = { 5, 0, 0, 1, 2, 3, 4, 5 };  // night, dawn, dawn, sunrise, day, sunset, dusk, night
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

			// Assign to output arrays
			commonData.timeOfDay1[0] = factors[0];  // dawn
			commonData.timeOfDay1[1] = factors[1];  // sunrise
			commonData.timeOfDay1[2] = factors[2];  // day
			commonData.timeOfDay1[3] = factors[3];  // sunset
			commonData.timeOfDay2[0] = factors[4];  // dusk
			commonData.timeOfDay2[1] = factors[5];  // night
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

		commonData.timeOfDay2[2] = commonData.eInteriorFactor * commonData.eNightDayFactor;           // interior day
		commonData.timeOfDay2[3] = commonData.eInteriorFactor * (1.0f - commonData.eNightDayFactor);  // interior night
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
	const std::vector<std::string> formatTargets = {
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
	const std::vector<std::string> fixedSizeTargets = {
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
	Effect::SetVectorVariable(effect, "ScreenSize", commonData.screenSize, sizeof(commonData.screenSize));
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

	// Set up for copy operation
	context->OMSetRenderTargets(1, &a_dest, nullptr);
	context->OMSetDepthStencilState(nullptr, 0);

	// Set shaders
	context->VSSetShader(copyVertexShader.get(), nullptr, 0);
	context->PSSetShader(copyPixelShader.get(), nullptr, 0);

	// Set source texture
	context->PSSetShaderResources(0, 1, &a_source);

	// Draw fullscreen quad
	context->Draw(4, 0);
}

void EffectManager::ApplyColorCorrection(ID3D11UnorderedAccessView* textureUAV)
{
	if (!textureUAV || !colorCorrectionComputeShader || !colorCorrectionConstantBuffer) {
		logger::warn("[ENBPP] Invalid parameters or shaders not initialized for color correction");
		return;
	}

	auto& settingManager = SettingManager::GetSingleton();

	auto brightness = settingManager.GetValue<float>("Brightness", "COLORCORRECTION");
	auto gammaCurve = settingManager.GetValue<float>("GammaCurve", "COLORCORRECTION");

	if (brightness == 1.0f && gammaCurve == 1.0f)
		return;

	auto context = globals::d3d::context;

	// Update constant buffer with current settings
	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = context->Map(colorCorrectionConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr)) {
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
	resource.as(texture);
	D3D11_TEXTURE2D_DESC texDesc;
	texture->GetDesc(&texDesc);

	// Dispatch compute shader (8x8 thread groups)
	UINT dispatchX = (texDesc.Width + 7) / 8;
	UINT dispatchY = (texDesc.Height + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	// Clear bindings
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetShader(nullptr, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &nullCB);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void EffectManager::RenderEffectsList()
{
	enbDepthOfField.RenderImGui();
	enbBloom.RenderImGui();
	enbLens.RenderImGui();
	enbAdaptation.RenderImGui();
	enbEffect.RenderImGui();
	enbEffectPostPass.RenderImGui();
}