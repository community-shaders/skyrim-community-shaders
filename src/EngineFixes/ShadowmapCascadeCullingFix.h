#pragma once

struct ShadowmapCascadeCullingFix : EngineFix
{
	std::string GetName() override { return "Shadowmap Cascade Culling Fix"; }

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