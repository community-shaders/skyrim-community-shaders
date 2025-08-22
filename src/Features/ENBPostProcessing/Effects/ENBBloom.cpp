#include "ENBBloom.h"

#include "../EffectManager.h"
#include "../TextureManager.h"

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& textureManager = TextureManager::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureBloom = textureManager.GetCommonTexture("TextureBloom");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& effectManager = EffectManager::GetSingleton();

	auto downsampledInputSRV = effectManager.GetDownsampleTexture();

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInputSRV, *textureBloom, *textureHDRTemp);
}

void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& effectManager = EffectManager::GetSingleton();
	SetShaderResourceVariable("TextureDownsampled", effectManager.GetDownsampleTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}