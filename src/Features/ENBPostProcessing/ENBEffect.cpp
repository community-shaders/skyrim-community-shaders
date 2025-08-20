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

	// TODO: Need to copy to framebuffer
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

	float4 enbParams01{};

	if (ENBParams01 && ENBParams01->IsValid())
		ENBParams01->SetRawValue(&enbParams01, 0, sizeof(enbParams01));
}