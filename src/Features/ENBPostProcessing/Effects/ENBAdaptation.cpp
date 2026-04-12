#include "ENBAdaptation.h"

#include "../SettingManager.h"
#include "../TextureManager.h"

void ENBAdaptation::Execute()
{
	auto& textureManager = TextureManager::GetSingleton();

	auto* currentSRV = textureManager.GetDownsampleTextureBlurry();
	if (!currentSRV) {
		return;
	}

	SetShaderResourceVariable("TextureCurrent", currentSRV);

	auto it = effectTextureCache.find("TextureCurrent");
	if (it == effectTextureCache.end()) {
		return;
	}

	ExecuteTechnique("Downsample", it->second);

	SetShaderResourceVariable("TextureCurrent", it->second.srv.get());

	// Use swap mechanism to determine input/output
	const char* texturePreviousName = (textureManager.GetTextureSwap() & 1) ? "TextureAdaptationSwap" : "TextureAdaptation";
	const char* textureAdaptationName = (textureManager.GetTextureSwap() & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";

	// Set input texture (previous frame's adaptation value)
	auto* texturePrevious = textureManager.GetCommonTexture(texturePreviousName);
	if (!texturePrevious) {
		return;
	}
	SetShaderResourceVariable("TexturePrevious", texturePrevious->srv.get());

	// Execute adaptation technique, writing to output texture
	auto* textureAdaptation = textureManager.GetCommonTexture(textureAdaptationName);
	if (!textureAdaptation) {
		return;
	}
	ExecuteTechnique("Draw", *textureAdaptation);
}

void ENBAdaptation::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();

	auto forceMinMaxValues = settingManager.GetValue<bool>("ForceMinMaxValues", "ADAPTATION");

	float adaptationTime = settingManager.GetValue<float>("AdaptationTime", "ADAPTATION");
	float deltaTime = (globals::game::deltaTime) ? (*globals::game::deltaTime) : 0.0f;

	float4 adaptationParameters{};
	adaptationParameters.x = !forceMinMaxValues ? 0.0f : settingManager.GetValue<float>("AdaptationMin", "ADAPTATION");
	adaptationParameters.y = !forceMinMaxValues ? 65535.0f : settingManager.GetValue<float>("AdaptationMax", "ADAPTATION");
	adaptationParameters.z = settingManager.GetValue<float>("AdaptationSensitivity", "ADAPTATION");
	adaptationParameters.w = (adaptationTime > 0.0f) ? (deltaTime / adaptationTime) : 1.0f;

	SetVectorVariable("AdaptationParameters", &adaptationParameters, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	effectTextureCache["TextureCurrent"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
}