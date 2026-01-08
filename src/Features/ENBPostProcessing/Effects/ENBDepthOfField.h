#pragma once

#include "Effect.h"

class ENBDepthOfField : public Effect
{
public:
	virtual std::string GetName() const override { return "enbdepthoffield.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

protected:
	// Override virtual texture creation function
	void CreateEffectTextures() override;
};