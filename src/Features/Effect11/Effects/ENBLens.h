#pragma once

#include "Effect.h"

/**
 * Provide the effect's name identifier.
 *
 * @returns The effect name "enblens.fx".
 */
/**
 * Execute the effect's main rendering or processing logic.
 */
/**
 * Update effect-related variables used by the effect prior to or during execution.
 */
class ENBLens : public Effect
{
public:
	virtual std::string GetName() const override { return "enblens.fx"; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};