#pragma once

#include "Effect.h"

class ENBAdaptation : public Effect
{
public:
	virtual std::string GetName() const override { return "enbadaptation.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

protected:
	// Override virtual texture creation function
	void CreateEffectTextures() override;
};