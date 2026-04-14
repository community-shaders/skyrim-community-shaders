#pragma once

#include "Effect.h"

/**
 * ENB post-pass rendering effect derived from Effect.
 *
 * Implements the post-processing pass used by ENB; provides effect identity,
 * execution entry point, and a hook to refresh shader/effect variables before execution.
 */
 
/**
 * Get the filename of the effect shader used by this effect.
 * @returns The effect filename "enbeffectpostpass.fx".
 */

/**
 * Execute the effect's rendering pass.
 *
 * Performs all work required to run the post-pass, including issuing draw calls
 * and applying the effect to the current render targets.
 */
 
/**
 * Update shader/effect variables used by the effect.
 *
 * Refreshes any per-frame or per-pass parameters (textures, constants, samplers)
 * so the effect executes with current state.
 */
class ENBEffectPostPass : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffectpostpass.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;
};