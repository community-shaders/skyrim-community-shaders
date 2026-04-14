#include "ENBAdaptation.h"

#include "../SettingManager.h"
#include "../TextureManager.h"

/**
 * @brief Produces the per-frame adaptation texture by downsampling the current blurred scene and blending it with the previous adaptation.
 *
 * Binds the current downsampled blurry texture as the source, runs the "Downsample" technique into an internal effect texture, then samples the previous-frame adaptation (selected via the texture swap parity) and executes the "Draw" technique to write the updated adaptation into the active adaptation render target. The method returns immediately if any required shader resource or target texture is unavailable.
 */
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

/**
 * @brief Updates and uploads shader adaptation parameters from runtime settings.
 *
 * Reads adaptation-related settings and the current frame delta time, builds a float4
 * containing min, max, sensitivity, and normalized adaptation speed, then uploads it
 * to the shader variable "AdaptationParameters".
 *
 * The components of the uploaded vector are:
 * - x: adaptation minimum value; uses 0.0 if `ForceMinMaxValues` is false, otherwise `AdaptationMin`.
 * - y: adaptation maximum value; uses 65535.0 if `ForceMinMaxValues` is false, otherwise `AdaptationMax`.
 * - z: adaptation sensitivity read from `AdaptationSensitivity`.
 * - w: normalized adaptation speed computed as `deltaTime / AdaptationTime` when `AdaptationTime > 0`, otherwise 1.0.
 */
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

/**
 * @brief Creates and caches the GPU texture used as the current adaptation input.
 *
 * Allocates a 16x16, single-channel (R32 float) texture named "ENBAdaptation::TextureCurrent"
 * and stores it in the effectTextureCache under the key "TextureCurrent".
 */
void ENBAdaptation::CreateEffectTextures()
{
	effectTextureCache["TextureCurrent"] = CreateTexture(16, 16, DXGI_FORMAT_R32_FLOAT, "ENBAdaptation::TextureCurrent");
}