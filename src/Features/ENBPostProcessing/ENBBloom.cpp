#include "ENBBloom.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include <imgui.h>

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	auto textureHDRTemp = effectManager.GetCommonTexture("TextureHDRTemp");
	auto textureBloom = effectManager.GetCommonTexture("TextureBloom");

	ExecuteTechniqueSequence(GetSelectedTechnique(), *textureHDRTemp, *textureBloom, *textureHDRTemp);
}

void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();
	UINT bloomMipLevel = downsampler.FindBestMipLevel(sharedChain, 1024, 1024);
	SetShaderResourceVariable("TextureDownsampled", downsampler.GetMipLevel(sharedChain, bloomMipLevel));

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}