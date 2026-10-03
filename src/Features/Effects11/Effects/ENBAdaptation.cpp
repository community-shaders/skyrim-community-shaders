#include "ENBAdaptation.h"

#include "../EffectManager.h"
#include "../SettingManager.h"
#include "../TextureManager.h"
#include "Features/Effects11.h"
#include "Features/SkySync.h"
#include "Globals.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

namespace
{
	constexpr float kSunMaskMarginRadians = DirectX::XMConvertToRadians(0.75f);
	constexpr uint32_t kSunMaskSize = 256;
}

void ENBAdaptation::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto* currentSRV = textureManager.GetDownsampleTextureBlurry();
	if (!currentSRV) {
		return;
	}

	// Adaptation measures the frame without the procedural sun disc, so the disc does not darken it
	currentSRV = MaskProceduralSun(currentSRV);

	SetShaderResourceVariable("TextureCurrent", currentSRV);

	if (!textureCurrent.texture)
		return;

	ExecuteTechnique("Downsample", textureCurrent);

	SetShaderResourceVariable("TextureCurrent", textureCurrent.srv.get());

	// Use swap mechanism to determine input/output
	const char* texturePreviousName = (textureManager.GetTextureSwap() & 1) ? "TextureAdaptationSwap" : "TextureAdaptation";
	const char* textureAdaptationName = (textureManager.GetTextureSwap() & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";

	// Set input texture (previous frame's adaptation value)
	auto* texturePrevious = textureManager.GetCommonTexture(texturePreviousName);
	if (!texturePrevious) {
		return;
	}
	SetShaderResourceVariable("TexturePrevious", texturePrevious->srv.get());

	// Execute adaptation technique, writing to output texture
	auto* textureAdaptation = textureManager.GetCommonTexture(textureAdaptationName);
	if (!textureAdaptation) {
		return;
	}
	ExecuteTechnique("Draw", *textureAdaptation);
}

bool ENBAdaptation::EnsureSunMaskResources()
{
	if (!textureSunMasked.texture || !textureSunMasked.rtv || !textureSunMasked.srv)
		return false;

	if (!sunMaskPS && !sunMaskPSFailed) {
		sunMaskPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Effects11\\AdaptationSunMaskPS.hlsl", {}, "ps_5_0"));
		if (!sunMaskPS) {
			logger::error("[EFFECTS11] Failed to compile AdaptationSunMaskPS.hlsl; the procedural sun stays visible to adaptation");
			sunMaskPSFailed = true;
		}
	}
	if (!sunMaskPS)
		return false;

	auto device = globals::d3d::device;

	if (!sunMaskCB) {
		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(SunMaskCB);
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&cbDesc, nullptr, sunMaskCB.put())))
			return false;
		Util::SetResourceName(sunMaskCB.get(), "ENBAdaptation::SunMaskCB");
	}

	if (!sunMaskSampler) {
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDesc, sunMaskSampler.put())))
			return false;
		Util::SetResourceName(sunMaskSampler.get(), "ENBAdaptation::SunMaskSampler");
	}

	return true;
}

ID3D11ShaderResourceView* ENBAdaptation::MaskProceduralSun(ID3D11ShaderResourceView* a_source)
{
	auto& settingManager = SettingManager::GetSingleton();
	const auto& effects11 = globals::features::effects11;
	if (!effects11.enableEffect || !settingManager.GetValue<bool>("EnableProceduralSun", "EFFECT") ||
		!settingManager.GetValue<bool>("ExcludeFromAdaptation", "PROCEDURALSUN"))
		return a_source;

	// Adaptation runs after the world pass, so gate on an exterior sky rather than State::inWorld
	auto sky = globals::game::sky;
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->sun || !sky->sun->root || !sky->root)
		return a_source;
	if (Util::GetSunVisibility(sky->sun) <= 0.0f)
		return a_source;

	const auto sunDirection = globals::features::skySync.GetCelestialDirection(sky, SkySync::Caster::Sun);

	if (!EnsureSunMaskResources())
		return a_source;

	auto context = globals::d3d::context;
	const auto& frame = globals::game::frameBufferCached;

	const float maskAngle = effects11.proceduralSunAngularRadius + kSunMaskMarginRadians;
	const float sampleAngle = maskAngle + kSunMaskMarginRadians;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(sunMaskCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return a_source;
	auto* cb = static_cast<SunMaskCB*>(mapped.pData);
	cb->CameraViewProj = frame.GetCameraViewProj();
	cb->CameraViewProjInverse = frame.GetCameraViewProjInverse();
	cb->SunDirection = { sunDirection.x, sunDirection.y, sunDirection.z, 0.0f };
	cb->DynamicResolution = { frame.GetDynamicResolutionParams1().x, frame.GetDynamicResolutionParams1().y, frame.GetDynamicResolutionParams2().x, frame.GetDynamicResolutionParams2().y };
	cb->MaskParams = { std::cos(maskAngle), std::cos(sampleAngle), std::sin(sampleAngle), 0.0f };
	context->Unmap(sunMaskCB.get(), 0);

	auto& effectManager = EffectManager::GetSingleton();

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(kSunMaskSize);
	viewport.Height = static_cast<float>(kSunMaskSize);
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	ID3D11RenderTargetView* rtv = textureSunMasked.rtv.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);
	context->OMSetDepthStencilState(nullptr, 0);
	context->RSSetState(effectManager.rasterizerState.get());
	context->OMSetBlendState(effectManager.blendState.get(), nullptr, 0xFFFFFFFF);

	UINT stride = sizeof(float) * 5;
	UINT offset = 0;
	ID3D11Buffer* vertexBuffers[] = { effectManager.quadVertexBuffer.get() };
	context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
	context->IASetInputLayout(effectManager.inputLayout.get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	context->VSSetShader(effectManager.copyVertexShader.get(), nullptr, 0);
	context->PSSetShader(sunMaskPS, nullptr, 0);
	context->PSSetShaderResources(0, 1, &a_source);
	ID3D11SamplerState* samplers[] = { sunMaskSampler.get() };
	context->PSSetSamplers(0, 1, samplers);
	ID3D11Buffer* constantBuffers[] = { sunMaskCB.get() };
	context->PSSetConstantBuffers(0, 1, constantBuffers);

	globals::profiler->BeginPass("Effects11::AdaptationSunMask");
	context->Draw(4, 0);
	globals::profiler->EndPass();

	ID3D11RenderTargetView* nullRTV = nullptr;
	context->OMSetRenderTargets(1, &nullRTV, nullptr);
	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(0, 1, &nullSRV);

	return textureSunMasked.srv.get();
}

void ENBAdaptation::ClearShaderCache()
{
	if (sunMaskPS) {
		sunMaskPS->Release();
		sunMaskPS = nullptr;
	}
	sunMaskPSFailed = false;
}

void ENBAdaptation::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();

	if (!idsCached) {
		idForceMinMax = settingManager.GetSettingID("ForceMinMaxValues", "ADAPTATION");
		idAdaptTime = settingManager.GetSettingID("AdaptationTime", "ADAPTATION");
		idAdaptMin = settingManager.GetSettingID("AdaptationMin", "ADAPTATION");
		idAdaptMax = settingManager.GetSettingID("AdaptationMax", "ADAPTATION");
		idAdaptSens = settingManager.GetSettingID("AdaptationSensitivity", "ADAPTATION");
		idsCached = true;
	}

	auto forceMinMaxValues = settingManager.GetValue<bool>(idForceMinMax);

	float adaptationTime = settingManager.GetValue<float>(idAdaptTime);
	float deltaTime = (globals::game::deltaTime) ? (*globals::game::deltaTime) : 0.0f;

	float4 adaptationParameters{};
	adaptationParameters.x = !forceMinMaxValues ? 0.0f : settingManager.GetValue<float>(idAdaptMin);
	adaptationParameters.y = !forceMinMaxValues ? 65535.0f : settingManager.GetValue<float>(idAdaptMax);
	adaptationParameters.z = settingManager.GetValue<float>(idAdaptSens);
	adaptationParameters.w = std::clamp((adaptationTime > 0.0f) ? (deltaTime / adaptationTime) : 1.0f, 0.0f, 1.0f);

	SetVectorVariable("AdaptationParameters", &adaptationParameters, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	textureCurrent = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
	textureSunMasked = CreateTexture(kSunMaskSize, kSunMaskSize, DXGI_FORMAT_R11G11B10_FLOAT, "ENBAdaptation::TextureSunMasked");
}
