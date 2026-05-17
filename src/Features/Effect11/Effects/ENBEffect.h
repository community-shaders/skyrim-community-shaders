#pragma once

#include "Effect.h"

class ENBEffect : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffect.fx"; }
	virtual bool IsRequired() const override { return true; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};