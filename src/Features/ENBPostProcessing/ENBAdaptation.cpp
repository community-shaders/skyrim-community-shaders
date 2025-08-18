#include "ENBAdaptation.h"
#include "Globals.h"
#include "State.h"
#include <imgui.h>
#include "EffectManager.h"

void ENBAdaptation::Execute(RE::BSGraphics::RenderTargetData& input,
	RE::BSGraphics::RenderTargetData& swap,
	RE::BSGraphics::RenderTargetData& output)
{
	UpdateAdaptationVariables();
	ExecuteTechniqueSequence(GetSelectedTechnique(), input, swap, output);
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
		
		AdaptationTexture texturePrevious{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, texturePrevious.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(texturePrevious.texture.Get(), nullptr, texturePrevious.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(texturePrevious.texture.Get(), nullptr, texturePrevious.srv.GetAddressOf()));
		
		adaptationTextures["TexturePrevious"] = std::move(texturePrevious);
	}

	// Create TextureCurrent (16x16 R32F)
	{
		texDesc.Width = 16;
		texDesc.Height = 16;
		
		AdaptationTexture textureCurrent{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, textureCurrent.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(textureCurrent.texture.Get(), nullptr, textureCurrent.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(textureCurrent.texture.Get(), nullptr, textureCurrent.srv.GetAddressOf()));
		
		adaptationTextures["TextureCurrent"] = std::move(textureCurrent);
	}

	// Create TextureAdaptation (1x1 R32F)
	{
		texDesc.Width = 1;
		texDesc.Height = 1;

		AdaptationTexture textureAdaptation{};
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

	// Set downsampled input texture for luminance calculation (target 1x1 for average luminance)
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();
	
	UINT adaptationMipLevel = downsampler.FindBestMipLevel(sharedChain, 16, 16);
	auto downsampledSRV = downsampler.GetMipLevel(sharedChain, adaptationMipLevel);
	
	auto downsampledInput = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid() && downsampledSRV) {
		downsampledInput->SetResource(downsampledSRV);
	}
}