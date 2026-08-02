#pragma once

#include "ExtendedEffect.h"

class ENBEffectPostPass : public EffectBase
{
public:
	virtual std::string GetName() const override { return "enbeffectpostpass.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;
};