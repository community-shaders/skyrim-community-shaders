#include "ENBEffect.h"
#include "Globals.h"
#include "State.h"

void ENBEffect::Execute()
{
	auto& effectManager = EffectManager::GetSingleton();

	auto textureColorTemp = effectManager.GetCommonTexture("TextureColorTemp");

	auto textureOriginal = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	Texture textureColor{};
	textureColor.texture = textureOriginal.texture;
	textureColor.srv = textureOriginal.SRV;
	textureColor.rtv = textureOriginal.RTV;

	UpdateEffectVariables();

	ExecuteTechniqueSequence(GetSelectedTechnique(), textureColor, *textureColorTemp);

	auto textureFramebuffer1 = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
	auto textureFramebuffer2 = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
	auto textureFramebuffer3 = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2];

	effectManager.CopyTexture(textureOriginal.SRV, textureFramebuffer1.RTV);
	effectManager.CopyTexture(textureOriginal.SRV, textureFramebuffer2.RTV);
	effectManager.CopyTexture(textureOriginal.SRV, textureFramebuffer3.RTV);
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

	if (ENBParams01 && ENBParams01->IsValid())
		ENBParams01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));

	auto textureBloom = effect->GetVariableByName("TextureBloom")->AsShaderResource();
	auto textureLens = effect->GetVariableByName("TextureLens")->AsShaderResource();
	auto textureAdaptation = effect->GetVariableByName("TextureAdaptation")->AsShaderResource();
	auto textureAperture = effect->GetVariableByName("TextureAperture")->AsShaderResource();

	if (textureBloom && textureBloom->IsValid()) {
		textureBloom->SetResource(effectManager.GetCommonTexture("TextureBloom")->srv.Get());
	}
	if (textureLens && textureLens->IsValid()) {
		textureLens->SetResource(effectManager.GetCommonTexture("TextureLens")->srv.Get());
	}
	if (textureAdaptation && textureAdaptation->IsValid()) {
		textureAdaptation->SetResource(effectManager.GetCommonTexture("TextureAdaptation")->srv.Get());
	}
	if (textureAperture && textureAperture->IsValid()) {
		textureAperture->SetResource(effectManager.GetCommonTexture("TextureAperture")->srv.Get());
	}
}