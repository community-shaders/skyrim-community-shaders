#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

TextureCube<float4> ReflectionTexture : register(t0);
RWTexture2D<sh2> IBLTexture : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(1, 1, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	// Initialise sh to 0
	sh2 shR = SphericalHarmonics::Zero();
	sh2 shG = SphericalHarmonics::Zero();
	sh2 shB = SphericalHarmonics::Zero();

	float axisSampleCount = 32;

	// Accumulate coefficients according to surounding direction/color tuples.
	for (float az = 0.5; az < axisSampleCount; az += 1.0)
		for (float ze = 0.5; ze < axisSampleCount; ze += 1.0) {
			float3 rayDir = SphericalHarmonics::GetUniformSphereSample(az / axisSampleCount, ze / axisSampleCount);

			float3 color = ReflectionTexture.SampleLevel(LinearSampler, -rayDir, 0);

			color = Color::GammaToLinear(color);

			sh2 sh = SphericalHarmonics::Evaluate(rayDir);

			shR = SphericalHarmonics::Add(shR, SphericalHarmonics::Scale(sh, color.r));
			shG = SphericalHarmonics::Add(shG, SphericalHarmonics::Scale(sh, color.g));
			shB = SphericalHarmonics::Add(shB, SphericalHarmonics::Scale(sh, color.b));
		}

	// Integrating over a sphere so each sample has a weight of 4*PI/samplecount (uniform solid angle, for each sample)
	float shFactor = 4.0 * Math::PI / (axisSampleCount * axisSampleCount);

	shR = SphericalHarmonics::Scale(shR, shFactor);
	shG = SphericalHarmonics::Scale(shG, shFactor);
	shB = SphericalHarmonics::Scale(shB, shFactor);

	IBLTexture[int2(0, 0)] = shR;
	IBLTexture[int2(1, 0)] = shG;
	IBLTexture[int2(2, 0)] = shB;
}
