#ifndef PROCEDURAL_SUN_HLSLI
#define PROCEDURAL_SUN_HLSLI

#include "Common/SunDisc.hlsli"

// Distinct from the Effects 11 ProceduralSun namespace: both headers can be included in one Sky shader
namespace StandaloneSun
{
	// The engine's fixed glare-visibility sample threshold assumes this sun-base half-width.
	static const float VANILLA_SUN_BASE_HALF_WIDTH = 425.0f;

	float GetOcclusionBillboardScale(float modelRadius)
	{
		return modelRadius > 0.0f ? sqrt(2.0f) * VANILLA_SUN_BASE_HALF_WIDTH / modelRadius : 1.0f;
	}

	float GetBillboardScale(float outerCos, float sunDistance, float billboardRadius)
	{
		if (!(outerCos > 0.0f && outerCos < 1.0f && sunDistance > 0.0f && billboardRadius > 0.0f))
			return 1.0f;
		// A square billboard's bounding radius is sqrt(2) times its half-width.
		return sunDistance * sqrt(2.0f * (1.0f - outerCos * outerCos)) / (outerCos * billboardRadius);
	}

	float3 ResizeBillboardVertex(float3 position, float4x4 world, float modelRadius, float outerCos)
	{
		float sunDistance = length(mul(world, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz);
		float worldScale = length(mul(world, float4(1.0f, 0.0f, 0.0f, 0.0f)).xyz);
		return position * GetBillboardScale(outerCos, sunDistance, modelRadius * worldScale);
	}

	float GetCloudTransmission(float cloudOpacity, float strength)
	{
		if (strength <= 0.0f || cloudOpacity <= 0.0f)
			return 1.0f;
		if (cloudOpacity >= 1.0f)
			return 0.0f;
		return pow(saturate(1.0f - cloudOpacity), strength);
	}
}

#endif
