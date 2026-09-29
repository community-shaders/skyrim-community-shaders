#ifndef __FLARE_OCCLUSION_DEPENDENCY_HLSL__
#define __FLARE_OCCLUSION_DEPENDENCY_HLSL__

#include "Common/Random.hlsli"

namespace FlareOcclusion
{
	static const uint SampleCount = 16;
	static const float SampleRadius = 0.02;  // UV, matches the vanilla lens flare visibility kernel

	/** @brief Screen UV offset of occlusion tap @p index around a light's projected position. */
	float2 GetSampleOffset(uint index)
	{
		return Random::PoissonSampleOffsets16[index] * SampleRadius;
	}

	/** @brief Maps the unoccluded tap count to visibility, so a few taps seeing sky through ridges barely leak. */
	float GetVisibility(uint visibleSamples)
	{
		return smoothstep(0.0, 1.0, visibleSamples / (float)SampleCount);
	}
}

#endif  // __FLARE_OCCLUSION_DEPENDENCY_HLSL__
