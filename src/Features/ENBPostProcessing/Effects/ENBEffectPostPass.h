#pragma once

#include "Effect.h"

class ENBEffectPostPass : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffectpostpass.fx"; }

	virtual void Execute() override;
};