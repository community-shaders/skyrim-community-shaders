#include "ENBBloom.h"

#include "EffectManager.h"
#include "TextureManager.h"
#include "ENBDownsampler.h"

void ENBBloom::Execute()
{
	// Get common textures for input/output
	auto& textureManager = TextureManager::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureBloom = textureManager.GetCommonTexture("TextureBloom");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& downsampler = ENBDownsampler::GetSingleton();

	ENBTexture downsampledInput{};
	downsampledInput.srv = downsampler.GetTexture();

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInput, *textureBloom, *textureHDRTemp);
}

void ENBBloom::UpdateEffectVariables()
{
	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	auto& downsampler = ENBDownsampler::GetSingleton();
	SetShaderResourceVariable("TextureDownsampled", downsampler.GetTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}