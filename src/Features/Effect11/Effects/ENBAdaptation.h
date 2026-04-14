#pragma once

#include "Effect.h"

/**
 * Get the filename of the shader/effect used by this effect.
 * @returns The literal string "enbadaptation.fx".
 */

/**
 * Execute the ENB adaptation effect's rendering pass and any associated GPU work.
 */

/**
 * Update shader/effect variables required by the ENB adaptation effect prior to execution.
 */

/**
 * Create and initialize any textures or render targets required specifically by the ENB adaptation effect.
 */
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