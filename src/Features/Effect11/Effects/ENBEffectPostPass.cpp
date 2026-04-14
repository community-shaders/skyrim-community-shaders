#include "ENBEffectPostPass.h"

#include "../TextureManager.h"

/**
 * @brief Executes the selected post-processing technique using temporary SDR textures.
 *
 * Retrieves the common textures "TextureSDRTemp" and "TextureSDRTemp2" and runs the currently
 * selected technique with "TextureSDRTemp" as the input SRV and the two textures as processing targets.
 * If the technique reports that it produced output into the alternate texture, the method swaps
 * "TextureSDRTemp" and "TextureSDRTemp2" in the TextureManager to reflect the updated contents.
 */
void ENBEffectPostPass::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");

	if (!textureSDRTemp || !textureSDRTemp2) {
		return;
	}

	bool inOutput = ExecuteTechniqueSequence(GetSelectedTechnique(), textureSDRTemp->srv.get(), *textureSDRTemp2, *textureSDRTemp);

	if (inOutput) {
		textureManager.SwapTextures("TextureSDRTemp", "TextureSDRTemp2");
	}
}

/**
 * @brief Updates shader variables related to the original SDR texture.
 *
 * If a common texture named "TextureSDRTemp" exists in the TextureManager, sets
 * the shader resource variable "TextureOriginal" to that texture's shader
 * resource view; otherwise performs no action.
 */
void ENBEffectPostPass::UpdateEffectVariables()
{
	auto& textureManager = TextureManager::GetSingleton();
	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	if (textureSDRTemp) {
		SetShaderResourceVariable("TextureOriginal", textureSDRTemp->srv.get());
	}
}