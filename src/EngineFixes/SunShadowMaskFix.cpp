#include "SunShadowMaskFix.h"

#include "Globals.h"

namespace
{
	constexpr uint32_t LightMaskBits = 32;
}

void SunShadowMaskFix::Install()
{
	mainAccumulator = reinterpret_cast<RE::BSShaderAccumulator**>(REL::RelocationID(528064, 415009).address());
	sunShadowSkipped = reinterpret_cast<const uint8_t*>(REL::RelocationID(528095, 415040).address());
	shadowDistance = reinterpret_cast<const float*>(REL::RelocationID(528314, 415263).address());

	stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);
}

void SunShadowMaskFix::Update()
{
	sunLightMask = 0;

	const auto* state = globals::game::smState;
	const auto* shadowSceneNode = state->shadowSceneNode[0];
	if (!shadowSceneNode || state->interior || *sunShadowSkipped)
		return;

	const auto& runtimeData = shadowSceneNode->GetRuntimeData();
	const auto* sunLight = runtimeData.sunShadowDirLight;
	if (!sunLight)
		return;

	// Each cascade holds its own slot, so the sun can own several bits.
	const auto& shadowLights = runtimeData.shadowLightsAccum;
	const uint32_t count = std::min(shadowLights.size(), LightMaskBits);
	for (uint32_t slot = 0; slot < count; ++slot) {
		if (shadowLights[slot] == sunLight)
			sunLightMask |= 1u << slot;
	}
}

bool SunShadowMaskFix::ShouldSetSunBits(const RE::BSLightingShaderProperty* property, const RE::BSGeometry* geometry, uint32_t renderMode, const RE::BSShaderAccumulator* accumulator)
{
	using enum RE::BSShaderProperty::EShaderPropertyFlag;

	if (!sunLightMask || !geometry || renderMode != static_cast<uint32_t>(RE::BSShaderAccumulator::RENDER_MODE::kNormal) || accumulator != *mainAccumulator)
		return false;

	const auto* lightData = property->lightData;
	// LOD and cloud geometry take their light from the sun light, not the sun shadow slots.
	if (!lightData || (lightData->activeLightMask & sunLightMask) || !property->flags.all(kReceiveShadows) || property->flags.any(kLODLandscape, kLODObjects, kCloudLOD, kHDLODObjects))
		return false;

	const auto& bound = geometry->worldBound;
	return (bound.center - accumulator->GetRuntimeData()->eyePosition).Length() - bound.radius < *shadowDistance;
}

RE::BSShaderProperty::RenderPassArray* SunShadowMaskFix::BSLightingShaderProperty_GetRenderPasses::thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, uint32_t renderMode, RE::BSShaderAccumulator* accumulator)
{
	if (ShouldSetSunBits(property, geometry, renderMode, accumulator))
		// Atomic so this worker write cannot drop bits the render thread's cascade cull sets concurrently.
		std::atomic_ref(property->lightData->activeLightMask).fetch_or(sunLightMask);
	return func(property, geometry, renderMode, accumulator);
}
