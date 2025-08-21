#include "ENBAdaptation.h"
#include "EffectManager.h"
#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

void ENBAdaptation::Execute()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();
	auto& sharedChain = effectManager.GetSharedDownsampleChain();

	auto downsampledInput = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid()) {
		UINT adaptationMipLevel = downsampler.FindBestMipLevel(sharedChain, 256, 256);
		downsampledInput->SetResource(downsampler.GetMipLevel(sharedChain, adaptationMipLevel));
	}

	ExecuteTechnique("Downsample", effectTextureCache["TextureCurrent"]);

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(effectTextureCache["TextureCurrent"].srv.Get());
	}

	// Use swap mechanism to determine input/output textures
	const std::string texturePreviousName = (effectManager.textureSwap & 1) ? "TextureAdaptationSwap" : "TextureAdaptation";
	const std::string textureAdaptationName = (effectManager.textureSwap & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";

	// Set input texture (previous frame's adaptation value)
	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(effectManager.GetCommonTexture(texturePreviousName)->srv.Get());
	}

	// Execute adaptation technique, writing to output texture
	auto* textureAdaptation = effectManager.GetCommonTexture(textureAdaptationName);
	ExecuteTechnique(GetSelectedTechnique(), *textureAdaptation);
}

void ENBAdaptation::UpdateEffectVariables()
{
	auto& effectManager = EffectManager::GetSingleton();

	float4 adaptationParameters{};
	adaptationParameters.x = effectManager.GetSetting<float>("ADAPTATION::AdaptationMin");
	adaptationParameters.y = effectManager.GetSetting<float>("ADAPTATION::AdaptationMax");
	adaptationParameters.z = effectManager.GetSetting<float>("ADAPTATION::AdaptationSensitivity");
	adaptationParameters.w = effectManager.GetSetting<float>("ADAPTATION::AdaptationTime") * (*globals::game::deltaTime);

	auto AdaptationParameters = effect->GetVariableByName("AdaptationParameters")->AsVector();
	if (AdaptationParameters && AdaptationParameters->IsValid())
		AdaptationParameters->SetRawValue(&adaptationParameters, 0, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	effectTextureCache["TextureCurrent"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
}