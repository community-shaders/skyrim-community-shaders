#include "ENBLens.h"

#include "../TextureManager.h"

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& textureManager = TextureManager::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureHDRTemp");
	auto textureLens = textureManager.GetCommonTexture("TextureLens");

	if (!textureHDRTemp || !textureLens) {
		return;
	}

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto downsampledInputSRV = TextureManager::GetSingleton().GetDownsampleTexture();

	if (!downsampledInputSRV) {
		return;
	}

	auto [executed, inOutput] = ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInputSRV, *textureLens, *textureHDRTemp);

	if (executed && !inOutput) {
		textureManager.SwapTextures("TextureLens", "TextureHDRTemp");
	}
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	SetShaderResourceVariable("TextureDownsampled", TextureManager::GetSingleton().GetDownsampleTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}