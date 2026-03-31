// HLSL Unit Tests for Common/Shading.hlsli
#include "/Shaders/Common/Shading.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

namespace TestConstants
{
	static const float EXACT_TOLERANCE = 0.001f;
	static const float APPROX_TOLERANCE = 0.01f;
}

/// @tags shading, ao, multibounce
[numthreads(1, 1, 1)] void TestMultiBounceAO() {
	float3 baseColorDark = float3(0.0, 0.0, 0.0);
	float3 baseColorMid = float3(0.5, 0.4, 0.3);
	float3 baseColorBright = float3(1.0, 0.9, 0.8);

	float3 blackResult = MultiBounceAO(baseColorDark, 0.35f);
	ASSERT(IsTrue, all(abs(blackResult - 0.35f) < TestConstants::EXACT_TOLERANCE));

	float3 zeroAO = MultiBounceAO(baseColorMid, 0.0f);
	ASSERT(IsTrue, all(abs(zeroAO) < TestConstants::EXACT_TOLERANCE));

	float3 fullAO = MultiBounceAO(baseColorBright, 1.0f);
	ASSERT(IsTrue, all(abs(fullAO - 1.0f) < TestConstants::APPROX_TOLERANCE));

	float3 lowAO = MultiBounceAO(baseColorMid, 0.25f);
	float3 highAO = MultiBounceAO(baseColorMid, 0.75f);
	ASSERT(IsTrue, all(highAO >= lowAO));

	float3 darkBounce = MultiBounceAO(float3(0.2, 0.2, 0.2), 0.5f);
	float3 brightBounce = MultiBounceAO(float3(0.8, 0.8, 0.8), 0.5f);
	ASSERT(IsTrue, all(brightBounce >= darkBounce));

	float3 bounded = MultiBounceAO(baseColorBright, 0.6f);
	ASSERT(IsTrue, all(!isnan(bounded)));
	ASSERT(IsTrue, all(!isinf(bounded)));
	ASSERT(IsTrue, all(bounded >= 0.6f));
	ASSERT(IsTrue, all(bounded <= 1.01f));
}

	/// @tags shading, ao, specular
	[numthreads(1, 1, 1)] void TestSpecularAOLagarde()
{
	float aoZero = SpecularAOLagarde(0.35f, 0.0f, 0.5f);
	ASSERT(IsTrue, abs(aoZero) < TestConstants::EXACT_TOLERANCE);

	float aoFull = SpecularAOLagarde(0.35f, 1.0f, 0.5f);
	ASSERT(IsTrue, abs(aoFull - 1.0f) < TestConstants::EXACT_TOLERANCE);

	float lowView = SpecularAOLagarde(0.2f, 0.5f, 0.4f);
	float highView = SpecularAOLagarde(0.9f, 0.5f, 0.4f);
	ASSERT(IsTrue, highView >= lowView);

	float smoothSurface = SpecularAOLagarde(0.2f, 0.5f, 0.1f);
	float roughSurface = SpecularAOLagarde(0.2f, 0.5f, 0.9f);
	ASSERT(IsTrue, roughSurface >= smoothSurface);

	float samples[4] = {
		SpecularAOLagarde(0.0f, 0.3f, 0.2f),
		SpecularAOLagarde(0.4f, 0.6f, 0.5f),
		SpecularAOLagarde(0.8f, 0.2f, 0.7f),
		SpecularAOLagarde(1.0f, 0.9f, 1.0f)
	};

	for (int i = 0; i < 4; i++) {
		ASSERT(IsTrue, !isnan(samples[i]));
		ASSERT(IsTrue, !isinf(samples[i]));
		ASSERT(IsTrue, samples[i] >= 0.0f);
		ASSERT(IsTrue, samples[i] <= 1.0f);
	}
}

/// @tags shading, ao, specular
[numthreads(1, 1, 1)] void TestSpecularOcclusion() {
	float occlusionZero = SpecularOcclusion(0.35f, 0.5f, 0.0f);
	ASSERT(IsTrue, abs(occlusionZero) < TestConstants::EXACT_TOLERANCE);

	float occlusionFull = SpecularOcclusion(0.35f, 0.5f, 1.0f);
	ASSERT(IsTrue, abs(occlusionFull - 1.0f) < TestConstants::EXACT_TOLERANCE);

	float lowView = SpecularOcclusion(0.2f, 0.5f, 0.4f);
	float highView = SpecularOcclusion(0.9f, 0.5f, 0.4f);
	ASSERT(IsTrue, highView >= lowView);

	float lowOcclusion = SpecularOcclusion(0.4f, 0.5f, 0.2f);
	float highOcclusion = SpecularOcclusion(0.4f, 0.5f, 0.8f);
	ASSERT(IsTrue, highOcclusion >= lowOcclusion);

	float lowAlpha = 0.1f;
	float highAlpha = 0.9f;
	float lowAlphaResult = SpecularOcclusion(0.2f, lowAlpha, 0.5f);
	float highAlphaResult = SpecularOcclusion(0.2f, highAlpha, 0.5f);
	ASSERT(IsTrue, lowAlphaResult >= highAlphaResult);

	float roughness = 0.6f;
	float derivedAlpha = roughness * roughness;
	float fromDerivedAlpha = SpecularOcclusion(0.45f, derivedAlpha, 0.5f);
	float fromExpandedAlpha = SpecularOcclusion(0.45f, roughness * roughness, 0.5f);
	ASSERT(IsTrue, abs(fromDerivedAlpha - fromExpandedAlpha) < TestConstants::EXACT_TOLERANCE);

	float exact = SpecularOcclusion(0.25f, 0.75f, 0.5f);
	float expected = saturate(pow(0.25f + 0.5f, 0.75f) - 1.0f + 0.5f);
	ASSERT(IsTrue, abs(exact - expected) < TestConstants::EXACT_TOLERANCE);

	float samples[4] = {
		SpecularOcclusion(0.0f, 0.2f, 0.3f),
		SpecularOcclusion(0.4f, 0.6f, 0.5f),
		SpecularOcclusion(0.8f, 0.9f, 0.2f),
		SpecularOcclusion(1.0f, 1.0f, 0.9f)
	};

	for (int i = 0; i < 4; i++) {
		ASSERT(IsTrue, !isnan(samples[i]));
		ASSERT(IsTrue, !isinf(samples[i]));
		ASSERT(IsTrue, samples[i] >= 0.0f);
		ASSERT(IsTrue, samples[i] <= 1.0f);
	}
}