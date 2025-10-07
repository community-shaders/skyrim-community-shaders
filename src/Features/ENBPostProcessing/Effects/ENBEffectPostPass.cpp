#include "ENBEffectPostPass.h"

#include "../TextureManager.h"

void ENBEffectPostPass::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");

	ExecuteTechniqueSequence(GetSelectedTechnique(), textureSDRTemp->srv.get(), *textureSDRTemp2, *textureSDRTemp);

	globals::d3d::context->CopyResource(textureSDRTemp->texture.get(), textureSDRTemp2->texture.get());
}