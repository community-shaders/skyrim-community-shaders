#include "ShadowmapCascadeCullingFix.h"
#include "Features/InteriorSun.h"
#include "Globals.h"

void ShadowmapCascadeCullingFix::Install()
{
	gfSplitOverlap = reinterpret_cast<float*>(REL::RelocationID(513805, 391863).address());

	stl::write_thunk_call<BSShadowDirectionalLight_SetFrameCamera_BuildCascadeCameraCullingPlanes>(REL::RelocationID(101499, 108496).address() + REL::Relocate(0x1B12, 0x1C02, 0x1C82));
}

void ShadowmapCascadeCullingFix::BSShadowDirectionalLight_SetFrameCamera_BuildCascadeCameraCullingPlanes::thunk(RE::BSShadowDirectionalLight* dirLight, RE::NiFrustumPlanes& outPlanes, FrustumSplit& frustumSplit, uint32_t splitCornerIndices[8], uint32_t numSplitCornerIndices, RE::NiPoint3& lightDir, RE::NiPoint3& cameraPos, uint32_t cornerOffsetIndex)
{
	func(dirLight, outPlanes, frustumSplit, splitCornerIndices, numSplitCornerIndices, lightDir, cameraPos, cornerOffsetIndex);

	auto& interiorSun = globals::features::interiorSun;
	const bool isInteriorSun = interiorSun.loaded && interiorSun.isInteriorWithSun;
	const bool singleCascadeMode = isInteriorSun && interiorSun.settings.ForceSingleShadowCascade;

	if (isInteriorSun) {
		// DISABLE frustum culling entirely for ALL interior sun scenarios
		// This allows geometry outside the view (like windows above/behind the camera)
		// to still cast shadows and produce volumetric lighting effects
		// The room/portal system already handles coarse culling via DirShadowLightCulling
		outPlanes.activePlanes = static_cast<RE::NiFrustumPlanes::ActivePlane>(0);
	} else if (!singleCascadeMode) {
		// This fix pulls the far face corners back towards the near face corners by double fSplitOverlap to provide an effective overlap of 1 * fSplitOverlap in each direction.
		// This corrects the vanilla behaviour which sets nearFace = farFace for the next cascade camera, where nearFace already includes +fSplitOverlap
		// which incorrectly pushes out the culling for the next cascade camera causing shadow gaps even at the default fSplitOverlap of 100.
		// This newly calculated farFace is not immediately used but will be copied into the nearFace for the next cascade camera and provide effective overlap.

		const float splitOverlap = *gfSplitOverlap * 2.0f;

		for (uint32_t i = 0; i < 4; ++i) {
			auto& nearCorner = frustumSplit.nearFace[i];
			auto& farCorner = frustumSplit.farFace[i];
			auto dir = farCorner - nearCorner;
			dir.Unitize();

			farCorner -= dir * splitOverlap;
		}
	}
}