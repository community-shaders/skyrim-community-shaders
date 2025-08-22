#include "ENBBloom.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include "TextureManager.h"
#include <imgui.h>

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();
	auto& textureManager = TextureManager::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureBloom = textureManager.GetCommonTexture("TextureBloom");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& downsampler = effectManager.GetDownsampler();

	ENBTexture downsampledInput{};
	downsampledInput.srv = downsampler.GetTexture();

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInput, *textureBloom, *textureHDRTemp);
}

void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	SetShaderResourceVariable("TextureDownsampled", downsampler.GetTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}