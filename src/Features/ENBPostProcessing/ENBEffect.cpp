#include "ENBEffect.h"
#include "Globals.h"
#include "State.h"

void ENBEffect::Execute()
{
	auto renderer = globals::game::renderer;

	auto textureOriginal = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto textureSwap = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
	auto textureSwap2 = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2];

	Texture inputTexture{};
	inputTexture.texture = textureOriginal.texture;
	inputTexture.srv = textureOriginal.SRV;
	inputTexture.rtv = textureOriginal.RTV;

	Texture outputTexture{};
	outputTexture.texture = textureSwap.texture;
	outputTexture.srv = textureSwap.SRV;
	outputTexture.rtv = textureSwap.RTV;

	Texture swapTexture{};
	swapTexture.texture = textureSwap2.texture;
	swapTexture.srv = textureSwap2.SRV;
	swapTexture.rtv = textureSwap2.RTV;

	UpdateEffectVariables();

	ExecuteTechniqueSequence(GetSelectedTechnique(), inputTexture, outputTexture, swapTexture);

	auto framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];

	ID3D11Resource* framebufferResource;
	framebuffer.RTV->GetResource(&framebufferResource);

	globals::d3d::context->CopyResource(framebufferResource, outputTexture.texture.Get());
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