#include "ENBEffect.h"
#include "Globals.h"
#include "State.h"

void ENBEffect::Execute()
{
	auto renderer = globals::game::renderer;

	auto& effectManager = EffectManager::GetSingleton();

	auto textureSDRTemp = effectManager.GetCommonTexture("TextureSDRTemp");
	auto textureSDRTemp2 = effectManager.GetCommonTexture("TextureSDRTemp2");

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	Texture textureColor{};
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

	float4 params01[7]{
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f }
	};

	params01[4].w = 0.0f;
	params01[5].w = 0.0f;

	if (Params01 && Params01->IsValid())
		Params01->SetRawValue(&params01, 0, sizeof(params01));

	auto& effectManager = EffectManager::GetSingleton();

	float4 enbParams01{};
	enbParams01.x = effectManager.ComputeTimeOfDayValue(effectManager.enbSettings.BLOOM.Amount);
	enbParams01.y = effectManager.ComputeTimeOfDayValue(effectManager.enbSettings.LENS.Amount);

	if (ENBParams01 && ENBParams01->IsValid())
		ENBParams01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));

	SetShaderResourceVariable("TextureBloom", effectManager.GetCommonTexture("TextureBloom")->srv.Get());
	SetShaderResourceVariable("TextureLens", effectManager.GetCommonTexture("TextureLens")->srv.Get());

	const std::string textureAdaptationName = (effectManager.textureSwap & 1) ? "TextureAdaptation" : "TextureAdaptationSwap";
	SetShaderResourceVariable("TextureAdaptation", effectManager.GetCommonTexture(textureAdaptationName)->srv.Get());
}