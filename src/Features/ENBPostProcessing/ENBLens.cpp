#include "ENBLens.h"

#include "ENBDownsampler.h"
#include "TextureManager.h"

void ENBLens::Execute()
{
	// Get common textures for input/output
	auto& textureManager = TextureManager::GetSingleton();
	auto& downsampler = ENBDownsampler::GetSingleton();

	auto textureHDRTemp = textureManager.GetCommonTexture("TextureBloomLensTemp");
	auto textureLens = textureManager.GetCommonTexture("TextureLens");

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	ENBTexture downsampledInput{};
	downsampledInput.srv = downsampler.GetTexture();

	ExecuteTechniqueSequence(GetSelectedTechnique(), downsampledInput, *textureLens, *textureHDRTemp);
}

void ENBLens::UpdateEffectVariables()
{
	if (!effect)
		return;

	// Set dowsampled texture, typically the one used (use 1024x1024 mip)
	SetShaderResourceVariable("TextureDownsampled", ENBDownsampler::GetSingleton().GetTexture());

	// Set original texture, not typically used due to aliasing
	SetShaderResourceVariable("TextureOriginal", globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
}