#pragma once

#include "Effect.h"

class ENBLens : public Effect
{
public:
	virtual std::string GetName() const override { return "enblens.fx"; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};