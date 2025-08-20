#include "ENBAdaptation.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"

void ENBAdaptation::Execute()
{
	UpdateAdaptationVariables();

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

	ExecuteTechnique("Downsample", nullInputTexture, adaptationTextures["TextureCurrent"]);

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(adaptationTextures["TextureCurrent"].srv.Get());
	}

	auto* textureAdaptation = effectManager.GetCommonTexture("TextureAdaptation");
	if (!textureAdaptation) {
		logger::error("ENBAdaptation: TextureAdaptation not available");
		return;
	}

	Texture tempTexture = *textureAdaptation;
	*effectManager.GetCommonTexture("TextureAdaptation") = adaptationTextures["TexturePrevious"];
	adaptationTextures["TexturePrevious"] = tempTexture;

	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(adaptationTextures["TexturePrevious"].srv.Get());
	}

	ExecuteTechnique(GetSelectedTechnique(), nullInputTexture, *effectManager.GetCommonTexture("TextureAdaptation"));
}

void ENBAdaptation::UpdateEffectVariables()
{
}

bool ENBAdaptation::Apply()
{
	// Call base Apply first
	bool result = Effect::Apply();
	if (result) {
		CreateAdaptationTextures();
	}
	return result;
}

void ENBAdaptation::Unload()
{
	adaptationTextures.clear();
	Effect::Unload();
}

void ENBAdaptation::CreateAdaptationTextures()
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

	// Create TexturePrevious (1x1 R32F)
	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture texturePrevious{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, texturePrevious.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(texturePrevious.texture.Get(), nullptr, texturePrevious.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(texturePrevious.texture.Get(), nullptr, texturePrevious.srv.GetAddressOf()));

		adaptationTextures["TexturePrevious"] = std::move(texturePrevious);
	}

	// Create TextureCurrent (16x16 R32F)
	{
		texDesc.Width = 16;
		texDesc.Height = 16;

		Texture textureCurrent{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureCurrent.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureCurrent.texture.Get(), nullptr, textureCurrent.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureCurrent.texture.Get(), nullptr, textureCurrent.srv.GetAddressOf()));

		adaptationTextures["TextureCurrent"] = std::move(textureCurrent);
	}

	// Create TextureAdaptation (1x1 R32F)
	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		Texture textureAdaptation{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureAdaptation.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureAdaptation.texture.Get(), nullptr, textureAdaptation.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureAdaptation.texture.Get(), nullptr, textureAdaptation.srv.GetAddressOf()));

		adaptationTextures["TextureAdaptation"] = std::move(textureAdaptation);
	}

	logger::info("Created adaptation textures: TexturePrevious (1x1), TextureCurrent (16x16), TextureAdaptation (1x1)");
}

void ENBAdaptation::UpdateAdaptationVariables()
{
	if (!effect)
		return;

	// Set adaptation textures
	for (auto& [name, adaptationTexture] : adaptationTextures) {
		auto variable = effect->GetVariableByName(name.c_str())->AsShaderResource();
		if (variable && variable->IsValid()) {
			variable->SetResource(adaptationTexture.srv.Get());
		}
	}

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