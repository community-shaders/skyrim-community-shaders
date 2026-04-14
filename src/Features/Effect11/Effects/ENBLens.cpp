#include "ENBLens.h"

#include "../TextureManager.h"

/**
 * @brief Executes the ENB lens effect using the downsampled input and shared render textures.
 *
 * @details Retrieves the shared textures "TextureHDRTemp" and "TextureLens" and the downsampled input SRV.
 * If any required texture or SRV is missing, the method returns without modifying textures. It runs the
 * selected technique sequence using the downsampled SRV as input and `TextureLens` / `TextureHDRTemp` as
 * outputs. If the technique sequence did not write to the expected output, the method swaps the contents
 * of the shared textures "TextureLens" and "TextureHDRTemp".
 */
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

	bool inOutput = ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInputSRV, *textureLens, *textureHDRTemp);

	if (!inOutput) {
		textureManager.SwapTextures("TextureLens", "TextureHDRTemp");
	}
}

/**
 * @brief Updates shader resource variables used by the ENBLens effect.
 *
 * If the effect object is not present, the function returns without modification.
 * When present, it binds the downsampled input SRV (uses the 1024×1024 mip) to the shader variable "TextureDownsampled"
 * and binds the main render target SRV to the shader variable "TextureOriginal".
 */
void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	SetShaderResourceVariable("TextureDownsampled", TextureManager::GetSingleton().GetDownsampleTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}