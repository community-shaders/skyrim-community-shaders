#pragma once

#include "Effect.h"

class ENBBloom : public Effect
{
public:
    virtual std::string GetEffectType() const override { return "enbbloom"; }
	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

    virtual void Execute(RE::BSGraphics::RenderTargetData& input, 
                        RE::BSGraphics::RenderTargetData& swap, 
                        RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();
};