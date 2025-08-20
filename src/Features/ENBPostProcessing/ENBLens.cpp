#include "ENBLens.h"
#include "EffectManager.h"
#include <imgui.h>

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	UpdateEffectVariables();

	auto textureHDRTemp = effectManager.GetCommonTexture("TextureHDRTemp");
	auto textureLens = effectManager.GetCommonTexture("TextureLens");

	ExecuteTechniqueSequence(GetSelectedTechnique(), *textureHDRTemp, *textureLens, *textureHDRTemp);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();
	UINT bloomMipLevel = downsampler.FindBestMipLevel(sharedChain, 1024, 1024);
	SetShaderResourceVariable("TextureDownsampled", downsampler.GetMipLevel(sharedChain, bloomMipLevel));

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}