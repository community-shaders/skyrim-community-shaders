#include "ShadowmapCascadeCullingFix.h"

void ShadowmapCascadeCullingFix::Install()
{
	gSplitOverlap = reinterpret_cast<float*>(REL::VariantOffset(0, 0x2035C88, 0x1ED65C0).address());

	stl::write_thunk_call<BSShadowDirectionalLight_SetFrameCamera_BuildCascadeCameraCullingPlanes>(REL::RelocationID(101499, 108496).address() + REL::Relocate(0, 0x1C02, 0));
}

void ShadowmapCascadeCullingFix::BSShadowDirectionalLight_SetFrameCamera_BuildCascadeCameraCullingPlanes::thunk(RE::BSShadowDirectionalLight* dirLight, RE::NiFrustumPlanes& outPlanes, FrustumSplit& frustumSplit, uint32_t splitCornerIndices[8], uint32_t numSplitCornerIndices, RE::NiPoint3& lightDir, RE::NiPoint3& cameraPos, uint32_t cornerOffsetIndex)
{
	func(dirLight, outPlanes, frustumSplit, splitCornerIndices, numSplitCornerIndices, lightDir, cameraPos, cornerOffsetIndex);

	// This fix pulls the far face corners back towards the near face corners by double fSplitOverlap to provide an effective overlap of 1 * fSplitOverlap in each direction.
	// This corrects the vanilla behaviour which sets nearFace = farFace for the next cascade camera, where nearFace already includes +fSplitOverlap
	// which incorrectly pushes out the culling for the next cascade camera causing shadow gaps even at the default fSplitOverlap of 100.
	// This newly calculated farFace is not immediately used but will be copied into the nearFace for the next cascade camera and provide effective overlap.

	const float splitOverlap = *gSplitOverlap * 2.0f;

	for (uint32_t i = 0; i < 4; ++i) {
		auto& nearCorner = frustumSplit.nearFace[i];
		auto& farCorner = frustumSplit.farFace[i];
		auto dir = farCorner - nearCorner;
		dir.Unitize();

		farCorner -= dir * splitOverlap;
	}
}