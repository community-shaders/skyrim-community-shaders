#include "ENBBloom.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include <imgui.h>

void ENBBloom::Execute(RE::BSGraphics::RenderTargetData& input,
	RE::BSGraphics::RenderTargetData& swap,
	RE::BSGraphics::RenderTargetData& output)
{
	UpdateBloomVariables();
	ExecuteTechniqueSequence(GetSelectedTechnique(), input, swap, output);
}

void ENBBloom::UpdateEffectVariables()
{
}

bool ENBBloom::Apply()
{
	// Call base Apply first
	bool result = Effect::Apply();
	if (result) {
		CreateBloomTextures();
	}
	return result;
}

void ENBBloom::Unload()
{
	bloomTextures.clear();
	Effect::Unload();
}

void ENBBloom::CreateBloomTextures()
{
	auto device = globals::d3d::device;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// Create fixed-size render targets for bloom
	std::vector<std::pair<std::string, UINT>> bloomSizes = {
		{ "RenderTarget1024", 1024 },
		{ "RenderTarget512", 512 },
		{ "RenderTarget256", 256 },
		{ "RenderTarget128", 128 },
		{ "RenderTarget64", 64 },
		{ "RenderTarget32", 32 },
		{ "RenderTarget16", 16 }
	};

	for (auto& [name, size] : bloomSizes) {
		texDesc.Width = size;
		texDesc.Height = size;

		BloomTexture bloomTexture{};
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, bloomTexture.texture.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateRenderTargetView(bloomTexture.texture.Get(), nullptr, bloomTexture.rtv.GetAddressOf()));
		DX::ThrowIfFailed(device->CreateShaderResourceView(bloomTexture.texture.Get(), nullptr, bloomTexture.srv.GetAddressOf()));

		bloomTextures[name] = std::move(bloomTexture);
	}

	logger::info("Created bloom render targets: 1024, 512, 256, 128, 64, 32, 16");
}

void ENBBloom::UpdateBloomVariables()
{
	if (!effect)
		return;

	// Set bloom fixed-size render targets
	for (auto& [name, bloomTexture] : bloomTextures) {
		auto variable = effect->GetVariableByName(name.c_str())->AsShaderResource();
		if (variable && variable->IsValid()) {
			variable->SetResource(bloomTexture.srv.Get());
		}
	}

	// Set downsampled input texture for bloom (target around 64x64 for performance)
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();

	UINT bloomMipLevel = downsampler.FindBestMipLevel(sharedChain, 64, 64);
	auto downsampledSRV = downsampler.GetMipLevel(sharedChain, bloomMipLevel);

	auto downsampledInput = effect->GetVariableByName("TextureDownsampled")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid() && downsampledSRV) {
		downsampledInput->SetResource(downsampledSRV);
	}
}