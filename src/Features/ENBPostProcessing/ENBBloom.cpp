#include "ENBBloom.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include <imgui.h>

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& effectManager = EffectManager::GetSingleton();

	auto textureHDRTemp = effectManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureBloom = effectManager.GetCommonTexture("TextureBloom");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedTexture = effectManager.GetSharedDownsampleTexture();

	Texture downsampledInput{};
	downsampledInput.srv = downsampler.GetTexture(sharedTexture);

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInput, *textureBloom, *textureHDRTemp);
}

void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedTexture = effectManager.GetSharedDownsampleTexture();
	SetShaderResourceVariable("TextureDownsampled", downsampler.GetTexture(sharedTexture));

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}