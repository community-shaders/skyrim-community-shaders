#include "ENBEffectPostPass.h"

#include "../TextureManager.h"

void ENBEffectPostPass::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");

	ExecuteTechniqueSequence(GetSelectedTechnique(), textureSDRTemp->srv.Get(), *textureSDRTemp2, *textureSDRTemp);

	// TODO: Do this cleaner
	globals::d3d::context->CopyResource(textureSDRTemp->texture.Get(), textureSDRTemp2->texture.Get());
}