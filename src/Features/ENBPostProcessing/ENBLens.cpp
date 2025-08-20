#include "ENBLens.h"
#include "EffectManager.h"
#include <imgui.h>

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	UpdateEffectVariables();

	auto textureColorTemp = effectManager.GetCommonTexture("TextureColorTemp");
	auto textureLens = effectManager.GetCommonTexture("TextureLens");

	ExecuteTechniqueSequence(GetSelectedTechnique(), *textureColorTemp, *textureLens);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used
	auto downsampledInput = effect->GetVariableByName("TextureDownsampled")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid()) {
		auto& effectManager = EffectManager::GetSingleton();
		auto& downsampler = effectManager.GetDownsampler();
		auto& sharedChain = effectManager.GetSharedDownsampleChain();
		UINT bloomMipLevel = downsampler.FindBestMipLevel(sharedChain, 1024, 1024);
		downsampledInput->SetResource(downsampler.GetMipLevel(sharedChain, bloomMipLevel));
	}

	// Set original texture, not typically used due to aliasing
	auto textureOriginal = effect->GetVariableByName("TextureOriginal")->AsShaderResource();
	if (textureOriginal && textureOriginal->IsValid()) {
		textureOriginal->SetResource(globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
	}
}