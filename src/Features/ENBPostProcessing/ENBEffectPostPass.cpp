#include "ENBEffectPostPass.h"
#include "EffectManager.h"
#include <imgui.h>

void ENBEffectPostPass::Execute()
{
	auto& effectManager = EffectManager::GetSingleton();

	auto textureSDRTemp = effectManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = effectManager.GetCommonTexture("TextureSDRTemp2");

	ExecuteTechniqueSequence(GetSelectedTechnique(), *textureSDRTemp, *textureSDRTemp2, *textureSDRTemp);

	// TODO: Do this cleaner
	globals::d3d::context->CopyResource(textureSDRTemp->texture.Get(), textureSDRTemp2->texture.Get());
}