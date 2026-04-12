#pragma once

#include "Effect.h"

class ENBBloom : public Effect
{
public:
	virtual std::string GetName() const override { return "enbbloom.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;
};