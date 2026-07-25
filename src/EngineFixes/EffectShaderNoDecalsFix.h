#pragma once

/**
 * @brief Prevents decals from being applied to soft-effect geometry.
 *
 * Particle-like geometry using BSEffectShaderProperty with kSoftEffect should
 * never receive decals (blood spatters, impact marks, etc.). The vanilla engine
 * does not set kNoDecals on these nodes, so decals can incorrectly attach to
 * billboard particles and other soft effects. This fix sets the kNoDecals flag
 * on any BSGeometry whose BSEffectShaderProperty carries kSoftEffect.
 */
struct EffectShaderNoDecalsFix : EngineFix
{
	std::string GetName() override { return "Effect Shader No Decals Fix"; }

	void Install() override;

private:
	struct BSEffectShaderProperty_SetupGeometry
	{
		static bool thunk(RE::BSEffectShaderProperty* a_property, RE::BSGeometry* a_geometry);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
