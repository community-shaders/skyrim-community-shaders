#include "ENBAdaptation.h"

#include "../SettingManager.h"
#include "../TextureManager.h"

void ENBAdaptation::Execute()
{
	auto downsampledInput = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (downsampledInput && downsampledInput->IsValid()) {
		// Use 256x256 mip for adaptation
		downsampledInput->SetResource(TextureManager::GetSingleton().GetDownsampleTextureBlurry());
	}

	ExecuteTechnique("Downsample", effectTextureCache["TextureCurrent"]);

	auto textureCurrent = effect->GetVariableByName("TextureCurrent")->AsShaderResource();
	if (textureCurrent && textureCurrent->IsValid()) {
		textureCurrent->SetResource(effectTextureCache["TextureCurrent"].srv.get());
	}

	// Use swap mechanism to determine input/output textures
	auto& settingManager = SettingManager::GetSingleton();
	const std::string texturePreviousName = (settingManager.GetTextureSwap() & 1) ? "TextureAdaptationSwap" : "TextureAdaptation";
	const std::string textureAdaptationName = (settingManager.GetTextureSwap() & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";

	// Set input texture (previous frame's adaptation value)
	auto& textureManager = TextureManager::GetSingleton();
	auto texturePrevious = effect->GetVariableByName("TexturePrevious")->AsShaderResource();
	if (texturePrevious && texturePrevious->IsValid()) {
		texturePrevious->SetResource(textureManager.GetCommonTexture(texturePreviousName)->srv.get());
	}

	// Execute adaptation technique, writing to output texture
	auto* textureAdaptation = textureManager.GetCommonTexture(textureAdaptationName);
	ExecuteTechnique(GetSelectedTechnique(), *textureAdaptation);
}

void ENBAdaptation::UpdateEffectVariables()
{
	auto& settingManager = SettingManager::GetSingleton();

	auto forceMinMaxValues = settingManager.GetValue<bool>("ForceMinMaxValues", "ADAPTATION");
	
	float4 adaptationParameters{};
	adaptationParameters.x = !forceMinMaxValues ? 0.0f : settingManager.GetValue<float>("AdaptationMin", "ADAPTATION");
	adaptationParameters.y = !forceMinMaxValues ? 65535.0f : settingManager.GetValue<float>("AdaptationMax", "ADAPTATION");
	adaptationParameters.z = settingManager.GetValue<float>("AdaptationSensitivity", "ADAPTATION");
	adaptationParameters.w = settingManager.GetValue<float>("AdaptationTime", "ADAPTATION") * (*globals::game::deltaTime);

	auto AdaptationParameters = effect->GetVariableByName("AdaptationParameters")->AsVector();
	if (AdaptationParameters && AdaptationParameters->IsValid())
		AdaptationParameters->SetRawValue(&adaptationParameters, 0, sizeof(adaptationParameters));
}

void ENBAdaptation::CreateEffectTextures()
{
	effectTextureCache["TextureCurrent"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
}