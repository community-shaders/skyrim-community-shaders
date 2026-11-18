// Community Shaders. Supercell NDF generation from a back template and mesocyclone core.
#include "PhysicalSky/StormData.hlsli"

cbuffer StormGeneration : register(b1) { CloudStormData Storm; };
Texture2D<float> Pattern : register(t0);
Texture2D<float2> BackHeight : register(t1);
Texture2D<float4> BackModeling : register(t2);
Texture2D<float2> CoreHeight : register(t3);
Texture2D<float4> CoreModeling : register(t4);
SamplerState WrapSampler : register(s0);
RWTexture2DArray<float4> Height : register(u0);
RWTexture2DArray<float4> Modeling : register(u1);

float2 Rotate(float2 p, float angle)
{
	float sine, cosine;
	sincos(angle, sine, cosine);
	return float2(cosine * p.x - sine * p.y, cosine * p.y + sine * p.x);
}

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	uint width, height, layers;
	Height.GetDimensions(width, height, layers);
	if (any(pixel >= uint2(width, height)))
		return;
	const float2 p = ((pixel + 0.5) / float2(width, height) - 0.5) * (2.0 * Storm.animation.z);
	const float level = Storm.generation.z;
	// Cache the weather-independent terms of ApplySupercell. The density pass
	// supplies the live weather NDF before evaluating its vertical profile.
	float2 bounds = float2(level, 1.0);
	float3 model = 0.0;
	float coreCoverageContribution = 0.0;
	float authoredResidual = 1.0;
	float stormMask = 0.0;
	float4 back = 0.0;
	const float strength = Storm.centerRadiusStrength.w;
	bool inside = true;
	if ((uint)Storm.generation.w & 1u) {
		float2 rotated = Rotate(p, -Storm.generation.x);
		inside = all(abs(rotated) < Storm.animation.w);
		if (inside) {
			float2 uv = rotated / (2.0 * Storm.animation.w);
			uv.x *= Storm.generation.y > 0.0 ? -1.0 : 1.0;
			uv += 0.5;
			back = saturate(BackModeling.SampleLevel(WrapSampler, uv, 0));
			const float weight = strength * back.a;
			authoredResidual *= 1.0 - weight;
			bounds = lerp(bounds, BackHeight.SampleLevel(WrapSampler, uv, 0) - 0.5 + level, weight);
			model = back.rgb * weight;
		}
	}
	const float radius = length(p) / Storm.centerRadiusStrength.z;
	const bool authoredCore = ((uint)Storm.generation.w & 2u) != 0u;
	if (authoredCore) {
		if (all(abs(p) < Storm.centerRadiusStrength.z)) {
			const float2 uv = p / (2.0 * Storm.centerRadiusStrength.z) + 0.5;
			const float2 heights = saturate(CoreHeight.SampleLevel(WrapSampler, uv, 0));
			if (heights.y > heights.x) {
				const float4 core = saturate(CoreModeling.SampleLevel(WrapSampler, uv, 0));
				if (((uint)Storm.generation.w & 4u) != 0u) {
					bounds = heights;
					model = core.rgb * strength;
				} else {
					bounds = lerp(bounds, heights, strength);
					model = lerp(model, core.rgb, strength);
					authoredResidual *= 1.0 - strength;
				}
				stormMask = core.a * strength;
			}
		}
	} else {
		if (inside && radius <= 1.0) {
			const float remaining = 1.0 - radius;
			const float2 twisted = Rotate(p, remaining * remaining * remaining * -6.2831855);
			const float pattern = Pattern.SampleLevel(WrapSampler, twisted * 0.00036621094, 0);
			const float turbulence = sqrt(saturate((pattern * 2.0 - 0.8 + radius * 0.2) / (0.7 + radius * 0.2)));
			const float shiftedRadius = length(p - float2(0.0, 50.0)) / Storm.centerRadiusStrength.z;
			const float radialRamp = saturate(radius * 2.5300357);
			const float shifted = sqrt(1.0 - saturate((shiftedRadius - 0.07666673) * 1.3274337)) * turbulence;
			const float shoulder = 1.0 - saturate((radius - 0.3) * 3.3333333);
			float bottom = level + 0.06 + max(turbulence * 0.2 * (1.0 - pow(abs(radius), 0.2)), 0.1 - saturate(radius * 10.0) * 0.1) + shoulder * shoulder * 0.04 -
			               pow(1.0 - saturate((radius - 0.2) * 5.0), 0.6) * 0.1;
			float coreCoverage = saturate(max(shifted * 2.0, 0.75 * (1.0 - radialRamp)));
			float coreTop = max(pow(1.0 - saturate((radius - 0.07666673) * 1.3274337), 2.2) * shifted,
				pow(saturate(0.8333334 * (1.0 - radialRamp)), 0.4));
			float coreBottom = pow(1.0 - saturate((radius - 0.0828) * 1.7760727), 0.25);
			stormMask = strength * remaining;
			const float coreCeiling = 1.0;

			const float coverageWeight = saturate(stormMask * 10.0);
			coreCoverageContribution = strength * coreCoverage * coverageWeight;
			bounds = lerp(bounds, float2(bottom, coreCeiling), strength * saturate(stormMask * 7.0));
			model.g = lerp(model.g, coreTop, strength * saturate(stormMask * 3.0));
			model.b = lerp(model.b, coreBottom, strength * coverageWeight);
			const float blended = lerp(model.r, coreCoverage, strength * coverageWeight);
			model.r = lerp(blended, max(model.r, strength * coreCoverage * coverageWeight), back.a);
		}
	}
	if (((uint)Storm.generation.w & 4u) != 0u) {
		Height[uint3(pixel, 0)] = float4(saturate(bounds), 0, 0);
		Modeling[uint3(pixel, 0)] = saturate(float4(model, stormMask));
	} else if (authoredCore) {
		Height[uint3(pixel, 0)] = float4(bounds - float2(level, 1.0) * authoredResidual, 1.0 - authoredResidual, 0);
		Modeling[uint3(pixel, 0)] = float4(model, stormMask);
	} else {
		const float heightResidual = (1.0 - strength * back.a) * (1.0 - strength * saturate(stormMask * 7.0));
		// RG: height intercept; B: back alpha; A: back coverage contribution.
		// Modeling R holds the core contribution so the max/lerp coverage
		// expression can be evaluated against live weather, not an empty baseline.
		Height[uint3(pixel, 0)] = float4(bounds - float2(level, 1.0) * heightResidual, back.a, strength * back.a * back.r);
		Modeling[uint3(pixel, 0)] = float4(coreCoverageContribution, model.gb, stormMask);
	}
}
