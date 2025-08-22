#include "ENBLens.h"

#include "../EffectManager.h"
#include "../TextureManager.h"

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& textureManager = TextureManager::GetSingleton();
	auto& effectManager = EffectManager::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureLens = textureManager.GetCommonTexture("TextureLens");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto downsampledInputSRV = effectManager.GetDownsampleTexture();

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInputSRV, *textureLens, *textureHDRTemp);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	SetShaderResourceVariable("TextureDownsampled", EffectManager::GetSingleton().GetDownsampleTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}