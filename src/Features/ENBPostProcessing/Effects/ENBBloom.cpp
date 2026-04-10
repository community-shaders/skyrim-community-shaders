#include "ENBBloom.h"

#include "../TextureManager.h"

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& textureManager = TextureManager::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureBloomTemp");
	auto textureBloom = textureManager.GetCommonTexture("TextureBloom");

	if (!textureHDRTemp || !textureBloom) {
		return;
	}

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto downsampledInputSRV = TextureManager::GetSingleton().GetDownsampleTexture();

	if (!downsampledInputSRV) {
		return;
	}

	bool inOutput = ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInputSRV, *textureBloom, *textureHDRTemp);

	if (!inOutput) {
		textureManager.SwapTextures("TextureBloom", "TextureBloomTemp");
	}
}

void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	SetShaderResourceVariable("TextureDownsampled", TextureManager::GetSingleton().GetDownsampleTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}