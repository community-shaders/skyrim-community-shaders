#include "ENBAdaptation.h"
#include "EffectManager.h"
#include "TextureManager.h"
#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

void ENBAdaptation::Execute()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& downsampler = effectManager.GetDownsampler();

	auto downsampledInput = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid()) {
		// Use 256x256 mip for adaptation
		downsampledInput->SetResource(downsampler.GetTextureBlurry());
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
	auto& textureManager = TextureManager::GetSingleton();
	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(textureManager.GetCommonTexture(texturePreviousName)->srv.Get());
	}

	// Execute adaptation technique, writing to output texture
	auto* textureAdaptation = textureManager.GetCommonTexture(textureAdaptationName);
	ExecuteTechnique(GetSelectedTechnique(), *textureAdaptation);
}

void ENBAdaptation::UpdateEffectVariables()
{
	auto& effectManager = EffectManager::GetSingleton();

	auto forceMinMaxValues = effectManager.GetSetting<bool>("ForceMinMaxValues", "ADAPTATION");

	float delta = (*globals::game::deltaTime);

	float4 adaptationParameters{};
	adaptationParameters.x = !forceMinMaxValues ? 0.0f : effectManager.GetSetting<float>("AdaptationMin", "ADAPTATION");
	adaptationParameters.y = !forceMinMaxValues ? 65535.0f : effectManager.GetSetting<float>("AdaptationMax", "ADAPTATION");
	adaptationParameters.z = effectManager.GetSetting<float>("AdaptationSensitivity", "ADAPTATION");
	adaptationParameters.w = delta / effectManager.GetSetting<float>("AdaptationTime", "ADAPTATION");

	auto AdaptationParameters = effect->GetVariableByName("AdaptationParameters")->AsVector();
	if (AdaptationParameters && AdaptationParameters->IsValid())
		AdaptationParameters->SetRawValue(&adaptationParameters, 0, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	effectTextureCache["TextureCurrent"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
}