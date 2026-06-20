#pragma once

/**
 * @brief Fixes shadow gaps caused by incorrect cascade camera culling plane overlap.
 *
 * The vanilla engine sets the near face of each successive cascade camera equal to
 * the previous cascade's far face, which already includes +fSplitOverlap. This
 * effectively doubles the overlap offset and pushes culling planes outward, causing
 * visible shadow gaps. This fix pulls far-face corners back by 2x fSplitOverlap to
 * produce the correct effective overlap of 1x fSplitOverlap in each direction.
 */
struct ShadowmapCascadeCullingFix : EngineFix
{
	/** @brief Returns the human-readable name of this fix. */
	std::string GetName() override { return "Shadowmap Cascade Culling Fix"; }

	/** @brief Installs the culling plane correction hook into the shadow cascade setup. */
	void Install() override;

private:
	inline static float* gfSplitOverlap = nullptr;

	struct BSShadowDirectionalLight_SetFrameCamera_BuildCascadeCameraCullingPlanes
	{
		struct FrustumSplit
		{
			RE::NiPoint3 nearFace[4];
			RE::NiPoint3 farFace[4];
		};

		static void thunk(RE::BSShadowDirectionalLight* dirLight, RE::NiFrustumPlanes& outPlanes, FrustumSplit& frustumSplit, uint32_t splitCornerIndices[8], uint32_t numSplitCornerIndices, RE::NiPoint3& lightDir, RE::NiPoint3& cameraPos, uint32_t cornerOffsetIndex);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};