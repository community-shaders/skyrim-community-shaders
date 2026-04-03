// HLSL Unit Tests for Hair/Hair.hlsli (Hair Specular feature)
//
// This test file tests the actual Hair.hlsli implementation by:
// 1. Defining necessary stubs for external dependencies (SharedData, textures, etc.)
// 2. Including the real Hair.hlsli file
// 3. Testing Hair namespace functions directly
#define CS_HAIR
#define HAIR
#define UNIT_TEST

// ============================================================================
// STUBS FOR EXTERNAL DEPENDENCIES (must be defined BEFORE including Hair.hlsli)
// ============================================================================

// Stub sampler required by Hair.hlsli
SamplerState SampColorSampler : register(s0);

// Stub SharedData namespace with hairSpecularSettings
// This must be defined before Hair.hlsli is included since it uses SharedData::hairSpecularSettings
namespace SharedData
{
	struct HairSpecularSettings
	{
		uint Enabled;
		float HairGlossiness;
		float SpecularMult;
		float DiffuseMult;
		uint EnableTangentShift;
		float PrimaryTangentShift;
		float SecondaryTangentShift;
		float HairSaturation;
		float SpecularIndirectMult;
		float DiffuseIndirectMult;
		float BaseColorMult;
		float Transmission;
		uint EnableSelfShadow;
		float SelfShadowStrength;
		float SelfShadowExponent;
		float SelfShadowScale;
		uint HairMode;
		uint3 pad;
	};

	// Test configuration with typical values
	static HairSpecularSettings hairSpecularSettings = {
		1,     // Enabled
		0.5f,  // HairGlossiness
		1.0f,  // SpecularMult
		1.0f,  // DiffuseMult
		0,     // EnableTangentShift (disabled for predictable tests)
		0.0f,  // PrimaryTangentShift
		0.5f,  // SecondaryTangentShift
		1.0f,  // HairSaturation
		1.0f,  // SpecularIndirectMult
		1.0f,  // DiffuseIndirectMult
		1.0f,  // BaseColorMult
		0.5f,  // Transmission
		0,     // EnableSelfShadow (disabled)
		0.5f,  // SelfShadowStrength
		2.0f,  // SelfShadowExponent
		1.0f,  // SelfShadowScale
		0,     // HairMode (0 = Scheuermann)
		uint3(0, 0, 0)
	};

	float GetScreenDepth(float2 uv, uint index = 0) { return 1.0f; }  // Stub function for screen depth, if needed by Hair.hlsli
}

// ============================================================================
// INCLUDE THE REAL HAIR.HLSLI AND ITS DEPENDENCIES
// ============================================================================
// Include common dependencies needed for tests (LightingCommon provides struct definitions)
#include "/Shaders/Common/LightingCommon.hlsli"
// Hair.hlsli includes: Common/BRDF.hlsli, Common/Color.hlsli, Common/Game.hlsli, Common/Math.hlsli
// These are all real files from the codebase that will be included automatically
#include "/Shaders/Hair/Hair.hlsli"

#include "/Test/STF/ShaderTestFramework.hlsli"

// Test tolerance constants
namespace TestConstants
{
	static const float FLOAT16_EPSILON = 0.001f;
	static const float APPROX_TOLERANCE = 0.01f;
	static const float EXACT_TOLERANCE = 0.0001f;
	static const float NEAR_ZERO = 0.0001f;
}

// ============================================================================
// TANGENT REORIENTATION TESTS
// ============================================================================

/// @tags hair, tangent
[numthreads(1, 1, 1)] void TestReorientTangent() {
	// Test: Tangent perpendicular to normal should remain unchanged
	float3 N = float3(0, 0, 1);
	float3 T = float3(1, 0, 0);  // Already perpendicular to N

	float3 T_reoriented = Hair::ReorientTangent(T, N);

	ASSERT(IsTrue, abs(dot(T_reoriented, N)) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(length(T_reoriented) - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Test: Non-perpendicular tangent should be projected
	float3 T_tilted = normalize(float3(1, 0, 0.5));
	float3 T_tilted_reoriented = Hair::ReorientTangent(T_tilted, N);

	ASSERT(IsTrue, abs(dot(T_tilted_reoriented, N)) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(length(T_tilted_reoriented) - 1.0f) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, tangent
	[numthreads(1, 1, 1)] void TestShiftTangent()
{
	float3 T = float3(1, 0, 0);
	float3 N = float3(0, 0, 1);

	// Zero shift should return original tangent (normalized)
	float3 T_noshift = Hair::ShiftTangent(T, N, 0.0f);
	ASSERT(IsTrue, abs(T_noshift.x - 1.0f) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(T_noshift.z) < TestConstants::EXACT_TOLERANCE);

	// Positive shift should tilt toward normal
	float3 T_shifted = Hair::ShiftTangent(T, N, 0.5f);
	ASSERT(IsTrue, T_shifted.z > 0.0f);  // Should have some z component
	ASSERT(IsTrue, abs(length(T_shifted) - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Negative shift should tilt away from normal
	float3 T_shifted_neg = Hair::ShiftTangent(T, N, -0.5f);
	ASSERT(IsTrue, T_shifted_neg.z < 0.0f);
}

// ============================================================================
// KAJIYA-KAY SPECULAR TESTS
// ============================================================================

/// @tags hair, specular, kajiya-kay
[numthreads(1, 1, 1)] void TestKajiyaKayBasic() {
	float3 T = float3(1, 0, 0);  // Tangent along X
	float3 H = float3(0, 1, 0);  // Half vector along Y (perpendicular to T)
	float shininess = 50.0f;

	float3 spec = Hair::D_KajiyaKay(T, H, shininess);

	// Perpendicular H to T should give maximum specular (sinTH = 1)
	ASSERT(IsTrue, spec.x > 0.0f);
	ASSERT(IsTrue, !isnan(spec.x) && !isinf(spec.x));

	// All channels should be equal (grayscale output)
	ASSERT(IsTrue, abs(spec.x - spec.y) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(spec.y - spec.z) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, specular, kajiya-kay
	[numthreads(1, 1, 1)] void TestKajiyaKayShininessEffect()
{
	float3 T = float3(1, 0, 0);
	float3 H = normalize(float3(0, 1, 0.5));

	// Higher shininess should give sharper highlights
	float3 spec_low = Hair::D_KajiyaKay(T, H, 10.0f);
	float3 spec_high = Hair::D_KajiyaKay(T, H, 100.0f);

	// Both should be positive
	ASSERT(IsTrue, spec_low.x > 0.0f);
	ASSERT(IsTrue, spec_high.x > 0.0f);

	// With same angle, higher shininess concentrates energy
	// At non-peak angle, high shininess should be lower
	ASSERT(IsTrue, spec_low.x != spec_high.x);
}

/// @tags hair, specular, kajiya-kay
[numthreads(1, 1, 1)] void TestKajiyaKayDirectionalAttenuation() {
	float3 T = float3(1, 0, 0);
	float shininess = 50.0f;

	// H parallel to T (TH = 1) -> dirAtten = saturate(1+1) = 1, but sinTH = 0
	float3 H_parallel = T;
	float3 spec_parallel = Hair::D_KajiyaKay(T, H_parallel, shininess);
	ASSERT(IsTrue, spec_parallel.x < TestConstants::EXACT_TOLERANCE);  // sinTH = 0

	// H anti-parallel to T (TH = -1) -> dirAtten = saturate(-1+1) = 0
	float3 H_anti = -T;
	float3 spec_anti = Hair::D_KajiyaKay(T, H_anti, shininess);
	ASSERT(IsTrue, spec_anti.x < TestConstants::EXACT_TOLERANCE);
}

	// ============================================================================
	// HAIR F0 TESTS
	// ============================================================================

	/// @tags hair, fresnel
	[numthreads(1, 1, 1)] void TestHairF0()
{
	float3 F0 = Hair::HairF0();

	// Hair has IOR of 1.55, F0 = ((1-n)/(1+n))^2 ≈ 0.046
	float expected = pow((1.0f - 1.55f) / (1.0f + 1.55f), 2);

	ASSERT(IsTrue, abs(F0.x - expected) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(F0.y - expected) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(F0.z - expected) < TestConstants::EXACT_TOLERANCE);

	// Should be around 0.046
	ASSERT(IsTrue, F0.x > 0.04f && F0.x < 0.05f);
}

// ============================================================================
// GAUSSIAN DISTRIBUTION TESTS
// ============================================================================

/// @tags hair, marschner, gaussian
[numthreads(1, 1, 1)] void TestHairGaussian() {
	float B = 0.3f;  // Beta (roughness)

	// At theta = 0, should be maximum
	float g_0 = Hair::Hair_g(B, 0.0f);

	// At theta = B, should be exp(-0.5) * 1/(sqrt(2*PI)*B) ≈ 0.606 * peak
	float g_B = Hair::Hair_g(B, B);

	ASSERT(IsTrue, g_0 > g_B);
	ASSERT(IsTrue, g_0 > 0.0f);
	ASSERT(IsTrue, g_B > 0.0f);

	// Should be symmetric
	float g_pos = Hair::Hair_g(B, 0.2f);
	float g_neg = Hair::Hair_g(B, -0.2f);
	ASSERT(IsTrue, abs(g_pos - g_neg) < TestConstants::EXACT_TOLERANCE);

	// Higher B (rougher) should have lower peak
	float g_rough = Hair::Hair_g(0.5f, 0.0f);
	float g_smooth = Hair::Hair_g(0.1f, 0.0f);
	ASSERT(IsTrue, g_smooth > g_rough);
}

	// ============================================================================
	// DIFFUSE ATTENUATION TESTS
	// ============================================================================

	/// @tags hair, diffuse, kajiya-kay
	[numthreads(1, 1, 1)] void TestHairDiffuseAttenuation()
{
	float3 N = float3(0, 0, 1);
	float3 V = normalize(float3(0, 0.5, 1));
	float3 L = normalize(float3(0, 0.5, 1));
	float3 baseColor = float3(0.5, 0.3, 0.2);  // Brown hair

	float3 diffuse = Hair::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 1.0f, baseColor);

	// Should be positive
	ASSERT(IsTrue, all(diffuse >= 0.0f));

	// Should be finite
	ASSERT(IsTrue, all(!isnan(diffuse)));
	ASSERT(IsTrue, all(!isinf(diffuse)));

	// With shadow < 1, should include scatter tint
	float3 diffuse_shadowed = Hair::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 0.5f, baseColor);
	ASSERT(IsTrue, all(diffuse_shadowed >= 0.0f));
}

/// @tags hair, diffuse, kajiya-kay
[numthreads(1, 1, 1)] void TestHairDiffuseBaseColorEffect() {
	float3 N = float3(0, 0, 1);
	float3 V = normalize(float3(0, 0.5, 1));
	float3 L = normalize(float3(0, 0.5, 1));

	float3 darkHair = float3(0.1, 0.08, 0.05);
	float3 lightHair = float3(0.8, 0.7, 0.5);

	float3 diffuse_dark = Hair::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 1.0f, darkHair);
	float3 diffuse_light = Hair::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 1.0f, lightHair);

	// Lighter hair should have more diffuse scattering (sqrt of baseColor)
	ASSERT(IsTrue, diffuse_light.x > diffuse_dark.x);
}

	// ============================================================================
	// SATURATION TESTS
	// ============================================================================

	/// @tags hair, saturation, color
	[numthreads(1, 1, 1)] void TestHairSaturation()
{
	float3 color = float3(0.8, 0.4, 0.2);

	// Saturation = 1 should return original color
	float3 result_1 = Hair::Saturation(color, 1.0f);
	ASSERT(IsTrue, abs(result_1.x - color.x) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(result_1.y - color.y) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(result_1.z - color.z) < TestConstants::EXACT_TOLERANCE);

	// Saturation = 0 should return grayscale (luminance)
	float3 result_0 = Hair::Saturation(color, 0.0f);
	float luma = Color::RGBToLuminance(color);
	ASSERT(IsTrue, abs(result_0.x - luma) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(result_0.y - luma) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(result_0.z - luma) < TestConstants::EXACT_TOLERANCE);

	// Saturation = 0.5 should be halfway
	float3 result_half = Hair::Saturation(color, 0.5f);
	float3 expected_half = lerp(float3(luma, luma, luma), color, 0.5f);
	ASSERT(IsTrue, abs(result_half.x - expected_half.x) < TestConstants::EXACT_TOLERANCE);
}

/// @tags hair, saturation, color
[numthreads(1, 1, 1)] void TestHairSaturationGrayscale() {
	// Grayscale input should be unaffected by saturation changes
	float3 gray = float3(0.5, 0.5, 0.5);

	float3 result_0 = Hair::Saturation(gray, 0.0f);
	float3 result_1 = Hair::Saturation(gray, 1.0f);
	float3 result_2 = Hair::Saturation(gray, 2.0f);

	ASSERT(IsTrue, abs(result_0.x - 0.5f) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(result_1.x - 0.5f) < TestConstants::EXACT_TOLERANCE);
	// Note: result_2 may be clamped by saturate()
}

	// ============================================================================
	// SHIFT NORMAL TESTS
	// ============================================================================

	/// @tags hair, normal, shift
	[numthreads(1, 1, 1)] void TestShiftNormal()
{
	float3 T = float3(1, 0, 0);
	float3 N = float3(0, 0, 1);

	// Zero shift should return original normal direction
	float3 N_noshift = Hair::ShiftNormal(T, N, 0.0f);
	ASSERT(IsTrue, abs(length(N_noshift) - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Shifted normal should still be unit length
	float3 N_shifted = Hair::ShiftNormal(T, N, 0.3f);
	ASSERT(IsTrue, abs(length(N_shifted) - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Shifted normal should be different from original when shift != 0
	float3 N_shifted2 = Hair::ShiftNormal(T, N, 0.5f);
	float dotNN = dot(N, N_shifted2);
	ASSERT(IsTrue, dotNN < 1.0f - TestConstants::EXACT_TOLERANCE);
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

/// @tags hair, edge-cases, robustness
[numthreads(1, 1, 1)] void TestKajiyaKayEdgeCases() {
	float3 T = float3(1, 0, 0);

	// Very high shininess
	float3 H = float3(0, 1, 0);
	float3 spec_high = Hair::D_KajiyaKay(T, H, 1000.0f);
	ASSERT(IsTrue, !isnan(spec_high.x) && !isinf(spec_high.x));
	ASSERT(IsTrue, spec_high.x >= 0.0f);

	// Very low shininess
	float3 spec_low = Hair::D_KajiyaKay(T, H, 1.0f);
	ASSERT(IsTrue, !isnan(spec_low.x) && !isinf(spec_low.x));
	ASSERT(IsTrue, spec_low.x >= 0.0f);

	// Normalized vectors (edge case: exactly parallel)
	float3 spec_para = Hair::D_KajiyaKay(T, T, 50.0f);
	ASSERT(IsTrue, !isnan(spec_para.x));
}

	/// @tags hair, edge-cases, robustness
	[numthreads(1, 1, 1)] void TestGaussianEdgeCases()
{
	// Very small B (very smooth hair)
	float g_smooth = Hair::Hair_g(0.01f, 0.0f);
	ASSERT(IsTrue, !isnan(g_smooth) && !isinf(g_smooth));
	ASSERT(IsTrue, g_smooth > 0.0f);

	// Large theta
	float g_large = Hair::Hair_g(0.3f, 2.0f);
	ASSERT(IsTrue, !isnan(g_large));
	ASSERT(IsTrue, g_large >= 0.0f);
	ASSERT(IsTrue, g_large < 1.0f);  // Should be very small
}

/// @tags hair, edge-cases, robustness
[numthreads(1, 1, 1)] void TestDiffuseAttenuationEdgeCases() {
	float3 baseColor = float3(0.5, 0.3, 0.2);

	// Colinear V and N
	float3 N = float3(0, 0, 1);
	float3 V = float3(0, 0, 1);
	float3 L = float3(0, 0, 1);

	float3 diffuse = Hair::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 1.0f, baseColor);
	ASSERT(IsTrue, all(!isnan(diffuse)));

	// Very dark hair (near black)
	float3 darkHair = float3(0.01, 0.01, 0.01);
	float3 diffuse_dark = Hair::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 0.5f, darkHair);
	ASSERT(IsTrue, all(!isnan(diffuse_dark)));
	ASSERT(IsTrue, all(!isinf(diffuse_dark)));
}

	/// @tags hair, saturation, edge-cases
	[numthreads(1, 1, 1)] void TestSaturationEdgeCases()
{
	// Very saturated color
	float3 saturatedColor = float3(1.0, 0.0, 0.0);
	float3 result = Hair::Saturation(saturatedColor, 1.5f);
	ASSERT(IsTrue, all(!isnan(result)));
	ASSERT(IsTrue, all(result >= 0.0f));
	ASSERT(IsTrue, all(result <= 1.0f));  // Should be clamped by saturate

	// Near-black color
	float3 darkColor = float3(0.001, 0.001, 0.001);
	float3 result_dark = Hair::Saturation(darkColor, 1.0f);
	ASSERT(IsTrue, all(!isnan(result_dark)));
}

// ============================================================================
// PHYSICAL PROPERTY TESTS
// ============================================================================

/// @tags hair, fresnel, physical
[numthreads(1, 1, 1)] void TestHairFresnelBehavior() {
	float3 F0 = Hair::HairF0();

	// At normal incidence, Fresnel should equal F0
	float3 F_normal = BRDF::F_Schlick(F0, 1.0f);
	ASSERT(IsTrue, abs(F_normal.x - F0.x) < TestConstants::EXACT_TOLERANCE);

	// At grazing angle, should approach 1.0
	float3 F_grazing = BRDF::F_Schlick(F0, 0.0f);
	ASSERT(IsTrue, abs(F_grazing.x - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Monotonically increasing as angle increases (VdotH decreases)
	float3 F_30 = BRDF::F_Schlick(F0, 0.866f);  // cos(30°)
	float3 F_60 = BRDF::F_Schlick(F0, 0.5f);    // cos(60°)
	ASSERT(IsTrue, F_60.x > F_30.x);
	ASSERT(IsTrue, F_30.x > F_normal.x);
}

	/// @tags hair, specular, energy-conservation
	[numthreads(1, 1, 1)] void TestKajiyaKayEnergyConservation()
{
	float3 T = float3(1, 0, 0);
	float shininess = 50.0f;

	// Sample multiple angles and verify specular doesn't exceed reasonable bounds
	float3 angles[4] = {
		float3(0, 1, 0),
		float3(0, 0, 1),
		normalize(float3(0, 1, 1)),
		normalize(float3(1, 1, 1))
	};

	for (int i = 0; i < 4; i++) {
		float3 H = angles[i];
		float3 spec = Hair::D_KajiyaKay(T, H, shininess);

		// Should be bounded (NDF normalized)
		ASSERT(IsTrue, spec.x >= 0.0f);
		ASSERT(IsTrue, spec.x < 100.0f);  // Reasonable upper bound
	}
}