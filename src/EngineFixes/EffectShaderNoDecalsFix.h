#pragma once

/**
 * @brief Prevents decals from being applied to soft-effect geometry.
 *
 * Particle-like geometry using BSEffectShaderProperty with kSoftEffect should
 * never receive decals (blood spatters, impact marks, etc.). The vanilla engine
 * does not set kNoDecals on these nodes, so decals can incorrectly attach to
 * billboard particles and other soft effects.
 *
 * Hooks BSTriShape::LinkObject (vtable 0x19) to set kNoDecals at NIF load
 * time, before the geometry enters the scene. Clones inherit the flag from
 * the template, so the write is a one-time creation-time patch.
 */
struct EffectShaderNoDecalsFix : EngineFix
{
	std::string GetName() override { return "Effect Shader No Decals Fix"; }

	void Install() override;

private:
	struct BSTriShape_LinkObject
	{
		static void thunk(RE::BSTriShape* a_this, RE::NiStream& a_stream);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
