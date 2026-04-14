#pragma once

#include "Effect.h"

/**
 * Get the effect's filename.
 *
 * @returns The literal filename "enbeffect.fx".
 */

/**
 * Execute the effect.
 */

/**
 * Update the effect's runtime variables prior to execution.
 */
class ENBEffect : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffect.fx"; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};