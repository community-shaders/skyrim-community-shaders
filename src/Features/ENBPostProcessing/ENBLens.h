#pragma once

#include "Effect.h"

class ENBLens : public Effect
{
public:
	virtual std::string GetName() const override { return "enblens.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute(RE::BSGraphics::RenderTargetData& input,
		RE::BSGraphics::RenderTargetData& swap,
		RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();
};