#pragma once

#include "Effect.h"

class ENBEffect : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffect.fx"; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};