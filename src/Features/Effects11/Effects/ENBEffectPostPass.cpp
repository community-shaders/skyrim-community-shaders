#include "ENBEffectPostPass.h"

#include "../TextureManager.h"

void ENBEffectPostPass::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");

	if (!textureSDRTemp || !textureSDRTemp2) {
		return;
	}

	auto [executed, inOutput] = ExecuteTechniqueSequence(GetSelectedTechnique(), textureSDRTemp->srv.get(), *textureSDRTemp2, *textureSDRTemp);

	if (executed && inOutput) {
		textureManager.SwapTextures("TextureSDRTemp", "TextureSDRTemp2");
	}
}

void ENBEffectPostPass::UpdateEffectVariables()
{
	auto* textureSDRTemp = GetCachedCommonTexture("TextureSDRTemp");
	SetShaderResourceVariable("TextureOriginal", textureSDRTemp ? textureSDRTemp->srv.get() : nullptr);
}