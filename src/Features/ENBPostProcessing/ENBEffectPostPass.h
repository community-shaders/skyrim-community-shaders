#pragma once

#include "Effect.h"

class ENBEffectPostPass : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffectpostpass.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute(RE::BSGraphics::RenderTargetData& input,
		RE::BSGraphics::RenderTargetData& swap,
		RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();
};