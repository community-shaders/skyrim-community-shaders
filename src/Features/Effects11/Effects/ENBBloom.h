#pragma once

#include "ExtendedEffect.h"

class ENBBloom : public EffectBase
{
public:
	virtual std::string GetName() const override { return "enbbloom.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;
};