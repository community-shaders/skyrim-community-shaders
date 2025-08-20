#include "ENBAdaptation.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

void ENBAdaptation::Execute()
{
	Texture nullInputTexture{};
	nullInputTexture.texture = nullptr;
	nullInputTexture.srv = nullptr;
	nullInputTexture.rtv = nullptr;

	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();

	auto downsampledInput = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid()) {
		UINT adaptationMipLevel = downsampler.FindBestMipLevel(sharedChain, 256, 256);
		downsampledInput->SetResource(downsampler.GetMipLevel(sharedChain, adaptationMipLevel));
	}

	ExecuteTechnique("Downsample", nullInputTexture, effectTextureCache["TextureCurrent"]);

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(effectTextureCache["TextureCurrent"].srv.Get());
	}

	// Use swap mechanism to determine input/output textures
	const std::string texturePreviousName = (effectManager.textureSwap & 1) ? "TextureAdaptationSwap" : "TextureAdaptation";
	const std::string textureAdaptationName = (effectManager.textureSwap & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";

	// Set input texture (previous frame's adaptation value)
	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(effectManager.GetCommonTexture(texturePreviousName)->srv.Get());
	}

	// Execute adaptation technique, writing to output texture
	auto* textureAdaptation = effectManager.GetCommonTexture(textureAdaptationName);
	ExecuteTechnique(GetSelectedTechnique(), nullInputTexture, *textureAdaptation);
	;
}

void ENBAdaptation::UpdateEffectVariables()
{
	// Set adaptation textures
	auto& effectManager = EffectManager::GetSingleton();

	float4 adaptationParameters{};
	adaptationParameters.x = effectManager.enbSettings.ADAPTATION.AdaptationMin;
	adaptationParameters.y = effectManager.enbSettings.ADAPTATION.AdaptationMax;
	adaptationParameters.z = effectManager.enbSettings.ADAPTATION.AdaptationSensitivity;
	adaptationParameters.w = effectManager.enbSettings.ADAPTATION.AdaptationTime * (*globals::game::deltaTime);

	auto AdaptationParameters = effect->GetVariableByName("AdaptationParameters")->AsVector();
	if (AdaptationParameters && AdaptationParameters->IsValid())
		AdaptationParameters->SetRawValue(&adaptationParameters, 0, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	auto device = globals::d3d::device;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;  // R32F format (red channel only)
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// Create TextureCurrent (16x16 R32F)
	{
		texDesc.Width = 16;
		texDesc.Height = 16;

		Texture textureCurrent{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureCurrent.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureCurrent.texture.Get(), nullptr, textureCurrent.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureCurrent.texture.Get(), nullptr, textureCurrent.srv.GetAddressOf()));

		Util::SetResourceName(textureCurrent.texture.Get(), "ENBAdaptation::TextureCurrent");
		Util::SetResourceName(textureCurrent.rtv.Get(), "ENBAdaptation::TextureCurrent RTV");
		Util::SetResourceName(textureCurrent.srv.Get(), "ENBAdaptation::TextureCurrent SRV");

		effectTextureCache["TextureCurrent"] = std::move(textureCurrent);
	}

	logger::info("Created adaptation textures: TextureCurrent (16x16)");
}