#ifndef PROCEDURAL_SUN_HLSLI
#define PROCEDURAL_SUN_HLSLI

namespace ProceduralSun
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

	// Hestroffer profile adapted from Physical Sky; see the accompanying MIT license.
	// http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
	float3 GetHestrofferLimbDarkening(float normalizedRadius)
	{
		float mu = sqrt(saturate(1.0f - normalizedRadius * normalizedRadius));

		float3 a0 = float3(0.34685f, 0.26073f, 0.15248f);
		float3 a1 = float3(1.37539f, 1.27428f, 1.38517f);
		float3 a2 = float3(-2.04425f, -1.30352f, -1.49615f);
		float3 a3 = float3(2.70493f, 1.47085f, 1.99886f);
		float3 a4 = float3(-1.94290f, -0.96618f, -1.48155f);
		float3 a5 = float3(0.55999f, 0.26384f, 0.44119f);

		float mu2 = mu * mu;
		float mu3 = mu2 * mu;
		float mu4 = mu2 * mu2;
		float mu5 = mu4 * mu;
		return max(a0 + a1 * mu + a2 * mu2 + a3 * mu3 + a4 * mu4 + a5 * mu5, 0.0f);
	}

	void EvaluateDisc(float cosTheta, float sunDiskCos, float edgeSoftness, out float3 limbDarkening, out float coverage)
	{
		limbDarkening = 0.0f;
		coverage = 0.0f;
		if (cosTheta <= sunDiskCos || sunDiskCos <= 0.0f || sunDiskCos >= 1.0f)
			return;

		float sunDiskSin = sqrt(max(1.0f - sunDiskCos * sunDiskCos, 1e-8f));
		float tanTheta = sqrt(saturate(1.0f - cosTheta * cosTheta)) / max(cosTheta, 1e-5f);
		float normalizedRadius = saturate(tanTheta * sunDiskCos / sunDiskSin);
		float edgeWidth = max((1.0f - sunDiskCos) * edgeSoftness, 1e-8f);

		limbDarkening = GetHestrofferLimbDarkening(normalizedRadius);
		coverage = saturate((cosTheta - sunDiskCos) / edgeWidth);
	}

	// Uses the Effects11 rational corona profile in angular cosine space.
	float EvaluateHalo(float cosTheta, float sunDiskCos, float sunHaloCos, float haloFalloff)
	{
		if (sunHaloCos >= sunDiskCos || cosTheta <= sunHaloCos)
			return 0.0f;

		float normalizedDistance = saturate((sunDiskCos - cosTheta) / max(sunDiskCos - sunHaloCos, 1e-8f));
		return (1.0f - normalizedDistance) * rcp(1.0f + max(haloFalloff, 0.0f) * normalizedDistance);
	}

	void ComposeDiscAndHalo(
		float3 limbDarkening,
		float discCoverage,
		float diskIntensity,
		float haloProfile,
		float haloIntensity,
		out float3 sunColor,
		out float sunCoverage)
	{
		haloProfile = haloIntensity > 0.0f ? saturate(haloProfile) : 0.0f;
		sunCoverage = max(saturate(discCoverage), haloProfile);
		float3 premultipliedSun = limbDarkening * diskIntensity * discCoverage + haloIntensity * haloProfile;
		sunColor = premultipliedSun / max(sunCoverage, 1e-5f);
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
