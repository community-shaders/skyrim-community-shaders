#include "ENBEffect.h"

#include "../SettingManager.h"
#include "../TextureManager.h"
#include "Utils/Game.h"

void ENBEffect::Execute()
{
	auto renderer = globals::game::renderer;

	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");

	if (!textureSDRTemp || !textureSDRTemp2) {
		return;
	}

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (!textureOriginal.SRV) {
		return;
	}

	// Execute with: input (16bit HDR), output (10bit SDR), temp (10bit SDR)
	bool inOutput = ExecuteTechniqueSequence(GetSelectedTechnique(), textureOriginal.SRV, *textureSDRTemp, *textureSDRTemp2);

	if (!inOutput) {
		textureManager.SwapTextures("TextureSDRTemp", "TextureSDRTemp2");
	}
}

void ENBEffect::UpdateEffectVariables()
{
	float4 params01[7]{};

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	if (!imageSpaceManager) {
		return;
	}

	GET_INSTANCE_MEMBER(data, imageSpaceManager);
	auto& baseData = data.baseData;

	auto& modAmount = data.modAmount;
	auto& modData = data.modData;

	params01[2].x = baseData.hdr.receiveBloomThreshold;
	params01[2].y = baseData.hdr.white * RE::GetINISetting("fReinhardWhiteScale:Display")->GetFloat();

	params01[3].x = baseData.cinematic.saturation;
	params01[3].z = baseData.cinematic.contrast;
	params01[3].w = baseData.cinematic.brightness;

	params01[4] = { baseData.tint.color.red,
		baseData.tint.color.green,
		baseData.tint.color.blue,
		baseData.tint.amount };

	params01[5] = { modData.data[RE::ImageSpaceModData::kFadeR] * modAmount,
		modData.data[RE::ImageSpaceModData::kFadeG] * modAmount,
		modData.data[RE::ImageSpaceModData::kFadeB] * modAmount,
		modData.data[RE::ImageSpaceModData::kFadeAmount] * modAmount };

	params01[6] = { 1, 1, 1, 1 };

	SetVectorVariable("Params01", &params01, sizeof(params01));

	auto& textureManager = TextureManager::GetSingleton();
	auto& settingManager = SettingManager::GetSingleton();

	float4 enbParams01{};
	enbParams01.x = settingManager.GetInterpolatedTimeOfDayValue("Amount", "BLOOM");
	enbParams01.y = settingManager.GetInterpolatedTimeOfDayValue("Amount", "LENS");

	SetVectorVariable("ENBParams01", &enbParams01, sizeof(enbParams01));

	auto bindTextureIfEnabled = [&](const char* settingKey, const char* shaderVar, const char* textureName) {
		ID3D11ShaderResourceView* srv = nullptr;
		if (settingManager.GetValue<bool>(settingKey, "EFFECT")) {
			auto* texture = textureManager.GetCommonTexture(textureName);
			srv = texture ? texture->srv.get() : nullptr;
		}
		SetShaderResourceVariable(shaderVar, srv);
	};

	bindTextureIfEnabled("EnableBloom", "TextureBloom", "TextureBloom");
	bindTextureIfEnabled("EnableLens", "TextureLens", "TextureLens");

	const char* adaptationTexName = (textureManager.GetTextureSwap() & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";
	bindTextureIfEnabled("EnableAdaptation", "TextureAdaptation", adaptationTexName);
}