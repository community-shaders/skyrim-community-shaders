#pragma once

#include "ExtendedEffect.h"

class ENBEffect : public EffectBase
{
public:
	virtual std::string GetName() const override { return "enbeffect.fx"; }
	virtual bool IsRequired() const override { return true; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;

private:
	uint32_t idBloomAmount = 0xFFFFFFFF;
	uint32_t idLensAmount = 0xFFFFFFFF;
	uint32_t idEnableBloom = 0xFFFFFFFF;
	uint32_t idEnableLens = 0xFFFFFFFF;
	uint32_t idEnableAdaptation = 0xFFFFFFFF;
	bool idsCached = false;
};