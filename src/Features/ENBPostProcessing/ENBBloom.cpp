#include "ENBBloom.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include <imgui.h>

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	UpdateBloomVariables();

	auto textureColorTemp = effectManager.GetCommonTexture("TextureColorTemp");
	auto textureBloom = effectManager.GetCommonTexture("TextureBloom");

	ExecuteTechniqueSequence(GetSelectedTechnique(), *textureColorTemp, *textureBloom);
}

bool ENBBloom::Apply()
{
	// Call base Apply first
	bool result = Effect::Apply();
	return result;
}

void ENBBloom::Unload()
{
	Effect::Unload();
}

void ENBBloom::UpdateBloomVariables()
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