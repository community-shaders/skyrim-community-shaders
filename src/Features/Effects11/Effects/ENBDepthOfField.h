#pragma once

#include "ExtendedEffect.h"

class ENBDepthOfField : public EffectBase
{
public:
	virtual std::string GetName() const override { return "enbdepthoffield.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

protected:
	void CreateEffectTextures() override;

private:
	uint32_t idApertureTime = 0xFFFFFFFF;
	uint32_t idFocusingTime = 0xFFFFFFFF;
	bool idsCached = false;
};
