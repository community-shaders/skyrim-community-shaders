#include "ENBEffect.h"

#include "SettingsManager.h"
#include "TextureManager.h"

void ENBEffect::Execute()
{
	auto renderer = globals::game::renderer;

	auto& textureManager = TextureManager::GetSingleton();

	auto textureSDRTemp = textureManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = textureManager.GetCommonTexture("TextureSDRTemp2");

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	ENBTexture textureColor{};
	textureColor.texture = textureOriginal.texture;
	textureColor.srv = textureOriginal.SRV;
	textureColor.rtv = textureOriginal.RTV;

	// Execute with: input (16bit HDR), output (10bit SDR), temp (10bit SDR)
	ExecuteTechniqueSequence(GetSelectedTechnique(), textureColor, *textureSDRTemp, *textureSDRTemp2);
}

void ENBEffect::UpdateEffectVariables()
{
	auto Params01 = effect->GetVariableByName("Params01")->AsVector();
	auto ENBParams01 = effect->GetVariableByName("ENBParams01")->AsVector();

	float4 params01[7]{};

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	auto& runtimeData = imageSpaceManager->GetRuntimeData();
	auto& baseData = runtimeData.data.baseData;

	auto& modAmount = runtimeData.data.modAmount;
	auto& modData = runtimeData.data.modData;

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

	if (Params01 && Params01->IsValid())
		Params01->SetRawValue(&params01, 0, sizeof(params01));

	auto& textureManager = TextureManager::GetSingleton();
	auto& settingsManager = SettingsManager::GetSingleton();

	float4 enbParams01{};
	enbParams01.x = settingsManager.GetInterpolatedTimeOfDayValue("Amount", "BLOOM");
	enbParams01.y = settingsManager.GetInterpolatedTimeOfDayValue("Amount", "LENS");

	if (ENBParams01 && ENBParams01->IsValid())
		ENBParams01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));

	SetShaderResourceVariable("TextureBloom", textureManager.GetCommonTexture("TextureBloom")->srv.Get());
	SetShaderResourceVariable("TextureLens", textureManager.GetCommonTexture("TextureLens")->srv.Get());

	const std::string textureAdaptationName = (settingsManager.GetTextureSwap() & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";
	SetShaderResourceVariable("TextureAdaptation", textureManager.GetCommonTexture(textureAdaptationName)->srv.Get());
}