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

	bool inOutput = ExecuteTechniqueSequence(GetSelectedTechnique(), textureSDRTemp->srv.get(), *textureSDRTemp2, *textureSDRTemp);

	if (inOutput) {
		textureManager.SwapTextures("TextureSDRTemp", "TextureSDRTemp2");
	}
}

void ENBEffectPostPass::UpdateEffectVariables()
{
	auto& textureManager = TextureManager::GetSingleton();
	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	if (textureSDRTemp) {
		SetShaderResourceVariable("TextureOriginal", textureSDRTemp->srv.get());
	}
}