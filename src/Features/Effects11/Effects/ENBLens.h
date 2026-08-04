#pragma once

#include "ExtendedEffect.h"

class ENBLens : public EffectBase
{
public:
	virtual std::string GetName() const override { return "enblens.fx"; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};