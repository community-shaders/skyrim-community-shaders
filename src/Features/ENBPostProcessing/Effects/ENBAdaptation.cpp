#include "ENBAdaptation.h"

#include "../EffectManager.h"
#include "../SettingsManager.h"
#include "../TextureManager.h"

void ENBAdaptation::Execute()
{
	auto& effectManager = EffectManager::GetSingleton();

	auto downsampledInput = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid()) {
		// Use 256x256 mip for adaptation
		downsampledInput->SetResource(effectManager.GetDownsampleTextureBlurry());
	}

	ExecuteTechnique("Downsample", effectTextureCache["TextureCurrent"]);

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(effectTextureCache["TextureCurrent"].srv.Get());
	}

	// Use swap mechanism to determine input/output textures
	auto& settingsManager = SettingsManager::GetSingleton();
	const std::string texturePreviousName = (settingsManager.GetTextureSwap() & 1) ? "TextureAdaptationSwap" : "TextureAdaptation";
	const std::string textureAdaptationName = (settingsManager.GetTextureSwap() & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";

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
	auto& settingsManager = SettingsManager::GetSingleton();

	auto forceMinMaxValues = settingsManager.GetValue<bool>("ForceMinMaxValues", "ADAPTATION");

	float4 adaptationParameters{};
	adaptationParameters.x = !forceMinMaxValues ? 0.0f : settingsManager.GetValue<float>("AdaptationMin", "ADAPTATION");
	adaptationParameters.y = !forceMinMaxValues ? 65535.0f : settingsManager.GetValue<float>("AdaptationMax", "ADAPTATION");
	adaptationParameters.z = settingsManager.GetValue<float>("AdaptationSensitivity", "ADAPTATION");
	adaptationParameters.w = *globals::game::deltaTime / settingsManager.GetValue<float>("AdaptationTime", "ADAPTATION");

	auto AdaptationParameters = effect->GetVariableByName("AdaptationParameters")->AsVector();
	if (AdaptationParameters && AdaptationParameters->IsValid())
		AdaptationParameters->SetRawValue(&adaptationParameters, 0, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	effectTextureCache["TextureCurrent"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
}