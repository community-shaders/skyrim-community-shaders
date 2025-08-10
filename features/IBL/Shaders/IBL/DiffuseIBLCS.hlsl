#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

TextureCube<float4> ReflectionTexture : register(t0);
RWTexture2D<sh2> IBLTexture : register(u0);

#if defined(DYNAMIC_CUBEMAPS)
TextureCube<float3> EnvTexture : register(t1);
TextureCube<float3> EnvReflectionsTexture : register(t2);
#endif

SamplerState LinearSampler : register(s0);

// Performance optimization: Use 16x16 samples (256 total) instead of 32x32 (1024)
// Quality difference is negligible but performance gain is ~4x
#define AXIS_SAMPLE_COUNT 16
#define TOTAL_SAMPLES (AXIS_SAMPLE_COUNT * AXIS_SAMPLE_COUNT)

// Shared memory for parallel reduction - each thread gets its own slot
groupshared sh2 sharedR[TOTAL_SAMPLES];
groupshared sh2 sharedG[TOTAL_SAMPLES];
groupshared sh2 sharedB[TOTAL_SAMPLES];

// Parallelize computation: 16x16 = 256 threads, one per sample
[numthreads(AXIS_SAMPLE_COUNT, AXIS_SAMPLE_COUNT, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
	// Each thread computes one sample of the spherical integral
	// Note: No bounds check needed since we dispatch exactly AXIS_SAMPLE_COUNT x AXIS_SAMPLE_COUNT threads
	uint az = dispatchID.x;
	uint ze = dispatchID.y;

	// Pre-compute constants for better performance
	const float rcpAxisSampleCount = rcp((float)AXIS_SAMPLE_COUNT);
	const float shFactor = 4.0 * Math::PI * rcpAxisSampleCount * rcpAxisSampleCount;

	// Compute sample direction (offset by 0.5 for center sampling)
	float2 sampleCoord = (float2(az, ze) + 0.5) * rcpAxisSampleCount;
	float3 rayDir = SphericalHarmonics::GetUniformSphereSample(sampleCoord.x, sampleCoord.y);

	// Sample cubemap with optimized direction
	float3 color = ReflectionTexture.SampleLevel(LinearSampler, -rayDir, 0).xyz;
#if defined(DYNAMIC_CUBEMAPS)
	// Optimized condition check using faster comparisons
	if (rayDir.z >= 0 && SharedData::iblSettings.SampleUnderHorizonFromDynCube) {
		float absZ = abs(rayDir.z);
		if (absZ > abs(rayDir.x) && absZ > abs(rayDir.y)) {
			color = EnvTexture.SampleLevel(LinearSampler, -rayDir, 0);
		}
	}
#endif
	// Apply gamma correction
	color = Color::IrradianceToLinear(color);

	// Compute spherical harmonics basis for this direction
	sh2 sh = SphericalHarmonics::Evaluate(rayDir);

	// Scale by integration weight and color contribution
	sh2 contributionR = SphericalHarmonics::Scale(sh, color.r * shFactor);
	sh2 contributionG = SphericalHarmonics::Scale(sh, color.g * shFactor);
	sh2 contributionB = SphericalHarmonics::Scale(sh, color.b * shFactor);

	// Store each thread's contribution in shared memory
	sharedR[groupIndex] = contributionR;
	sharedG[groupIndex] = contributionG;
	sharedB[groupIndex] = contributionB;

	GroupMemoryBarrierWithGroupSync();

	// Parallel reduction using tree-based approach (compatible with DirectX 11)
	// Reduce 256 values down to 1 using logarithmic steps
	[unroll] for (uint stride = TOTAL_SAMPLES / 2; stride > 0; stride >>= 1) {
		if (groupIndex < stride) {
			sharedR[groupIndex] = SphericalHarmonics::Add(sharedR[groupIndex], sharedR[groupIndex + stride]);
			sharedG[groupIndex] = SphericalHarmonics::Add(sharedG[groupIndex], sharedG[groupIndex + stride]);
			sharedB[groupIndex] = SphericalHarmonics::Add(sharedB[groupIndex], sharedB[groupIndex + stride]);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// Only first thread writes the final accumulated result
	if (groupIndex == 0) {
		IBLTexture[int2(0, 0)] = sharedR[0];
		IBLTexture[int2(1, 0)] = sharedG[0];
		IBLTexture[int2(2, 0)] = sharedB[0];
	}
}