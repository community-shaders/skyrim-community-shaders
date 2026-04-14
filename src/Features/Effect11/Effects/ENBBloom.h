#pragma once

#include "Effect.h"

/**
 * Get the effect's identifier string.
 * @returns The effect identifier "enbbloom.fx".
 */

/**
 * Execute the effect's main rendering logic.
 */

/**
 * Update the effect's shader/uniform variables and internal state before execution.
 */
class ENBBloom : public Effect
{
public:
	virtual std::string GetName() const override { return "enbbloom.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;
};