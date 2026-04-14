#include "ENBBloom.h"

#include "../TextureManager.h"

/**
 * @brief Performs the bloom effect pass and updates shared bloom textures.
 *
 * Uses the current downsampled input texture to run the selected bloom technique and write results into the shared
 * bloom textures managed by TextureManager. If required textures or the downsampled input are unavailable, the
 * function returns without modifying textures. If the technique produces output in the alternate target, the two
 * shared bloom textures are swapped to ensure the latest result is referenced by "TextureBloom".
 *
 * @note This function may modify the TextureManager's common textures (including swapping "TextureBloom" and
 * "TextureBloomTemp").
 */
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

/**
 * @brief Binds the bloom effect's shader resources for downsampled and original scene textures.
 *
 * Sets the shader variable "TextureDownsampled" to the current downsampled texture SRV and
 * sets "TextureOriginal" to the main render target SRV.
 */
void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	SetShaderResourceVariable("TextureDownsampled", TextureManager::GetSingleton().GetDownsampleTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}