#include "ENBLens.h"
#include "EffectManager.h"
#include <imgui.h>

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	// Create input texture from downsampled input
	Texture inputTexture{};
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();
	UINT bloomMipLevel = downsampler.FindBestMipLevel(sharedChain, 1024, 1024);
	auto downsampledSRV = downsampler.GetMipLevel(sharedChain, bloomMipLevel);

	// Create temp input from downsampled
	inputTexture.srv = ComPtr<ID3D11ShaderResourceView>(downsampledSRV);

	auto renderer = globals::game::renderer;
	auto textureSwap = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];

	Effect::Texture swapTexture{};
	swapTexture.texture = textureSwap.texture;
	swapTexture.srv.Attach(textureSwap.SRV);
	swapTexture.rtv.Attach(textureSwap.RTV);

	auto* textureLens = effectManager.GetCommonTexture("TextureLens");
	if (!textureLens) {
		logger::error("ENBLens: TextureLens not available");
		return;
	}

	ExecuteTechniqueSequence(GetSelectedTechnique(), inputTexture, *textureLens, swapTexture);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;
}