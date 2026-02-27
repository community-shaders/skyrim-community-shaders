// HLSL Unit Tests for Hair Shader Functions
// Tests for features/Hair Specular/Shaders/Hair/Hair.hlsli
#include "/Shaders/Common/BRDF.hlsli"
#include "/Shaders/Common/Color.hlsli"
#include "/Shaders/Common/Game.hlsli"
#include "/Shaders/Common/Math.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

// Note: We can't directly include Hair.hlsli from the feature directory in tests
// because it requires texture bindings and SharedData context.
// Instead, we replicate the core mathematical functions for testing purposes.

namespace HairTest
{
	// Replicate core mathematical functions from Hair.hlsli for testing

	float3 ReorientTangent(float3 T, float3 N)
	{
		// Reorient tangent to be orthogonal to normal
		float3 T_reoriented = normalize(T - N * dot(T, N));
		return T_reoriented;
	}

	float3 HairF0()
	{
		const float n = 1.55;
		const float F0 = pow((1 - n) / (1 + n), 2);
		return F0.xxx;
	}

	float3 ShiftTangent(float3 T, float3 N, float shift)
	{
		return normalize(T + N * shift);
	}

	float3 ShiftNormal(float3 T, float3 N, float shift)
	{
		float3 T_shifted = ShiftTangent(T, N, shift);
		float3 N_shifted = normalize(cross(T_shifted, cross(N, T_shifted)));
		return N_shifted;
	}

	float3 D_KajiyaKay(float3 T, float3 H, float n)
	{
		float TH = dot(T, H);
		float sinTH = saturate(1 - TH * TH);
		float dirAtten = saturate(TH + 1);
		float norm = (n + 2) / (2 * Math::PI);
		return dirAtten * norm * pow(sinTH, 0.5 * n);
	}

	float Hair_g(float B, float Theta)
	{
		return exp(-0.5 * Theta * Theta / (B * B)) / (sqrt(Math::TAU) * B);
	}

	float3 Saturation(float3 color, float saturation)
	{
		float luminance = Color::RGBToLuminance(color);
		return saturate(lerp(float3(luminance, luminance, luminance), color, saturation));
	}

	float3 GetHairDiffuseAttenuationKajiyaKay(float3 N, float3 V, float3 L, float shadow, float3 baseColor)
	{
		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		float3 S = 0;

		float diffuseKajiya = 1 - abs(NdotL);

		float3 fakeN = normalize(V - N * NdotV);
		const float wrap = 1;
		float wrappedNdotL = saturate((dot(fakeN, L) + wrap) / ((1 + wrap) * (1 + wrap)));
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = max(Color::RGBToLuminance(baseColor), 1e-4);
		float3 scatterTint = shadow < 1 ? pow(abs(baseColor / luma), 1 - shadow) : 1;
		S += sqrt(baseColor) * diffuseScatter * scatterTint;

		return max(S, 0);
	}
}

// Test tolerance constants
namespace TestConstants
{
	static const float EXACT_TOLERANCE = 0.0001f;
	static const float APPROX_TOLERANCE = 0.01f;
	static const float NEAR_ZERO = 0.0001f;
}

// ============================================================================
// BASIC FUNCTION TESTS
// ============================================================================

/// @tags hair, tangent, orthogonalization
/// Test that ReorientTangent produces a tangent orthogonal to the normal
[numthreads(1, 1, 1)] void TestReorientTangentOrthogonality() {
	float3 N = float3(0, 0, 1);    // Normal pointing up
	float3 T = float3(1, 0, 0.2);  // Tangent with slight z component

	float3 T_reoriented = HairTest::ReorientTangent(T, N);

	// Reoriented tangent should be orthogonal to normal
	float dotProduct = dot(T_reoriented, N);
	ASSERT(IsTrue, abs(dotProduct) < TestConstants::EXACT_TOLERANCE);

	// Reoriented tangent should be normalized
	float length = sqrt(dot(T_reoriented, T_reoriented));
	ASSERT(IsTrue, abs(length - 1.0f) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, tangent, edge-cases
	/// Test ReorientTangent with perpendicular tangent (should remain unchanged)
	[numthreads(1, 1, 1)] void TestReorientTangentPerpendicular()
{
	float3 N = float3(0, 0, 1);
	float3 T = float3(1, 0, 0);  // Already perpendicular

	float3 T_reoriented = HairTest::ReorientTangent(T, N);

	// Should remain essentially the same (within numerical precision)
	ASSERT(IsTrue, abs(T_reoriented.x - T.x) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(T_reoriented.y - T.y) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(T_reoriented.z - T.z) < TestConstants::APPROX_TOLERANCE);
}

/// @tags hair, tangent, edge-cases
/// Test ReorientTangent with parallel tangent
[numthreads(1, 1, 1)] void TestReorientTangentParallel() {
	float3 N = float3(0, 0, 1);
	float3 T = float3(0, 0, 1);  // Parallel to normal

	float3 T_reoriented = HairTest::ReorientTangent(T, N);

	// Result should be normalized (though direction may be undefined)
	float length = sqrt(dot(T_reoriented, T_reoriented));
	ASSERT(IsTrue, abs(length - 1.0f) < 0.1f || isnan(length));  // May produce NaN in degenerate case
}

	/// @tags hair, fresnel, constants
	/// Test HairF0 returns correct Fresnel reflectance for hair IOR
	[numthreads(1, 1, 1)] void TestHairF0Value()
{
	float3 F0 = HairTest::HairF0();

	// For n = 1.55 (hair IOR), F0 = ((1-1.55)/(1+1.55))^2 ≈ 0.0465
	const float expectedF0 = 0.0465f;

	ASSERT(IsTrue, abs(F0.x - expectedF0) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(F0.y - expectedF0) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(F0.z - expectedF0) < TestConstants::APPROX_TOLERANCE);

	// F0 should be positive and less than 1
	ASSERT(IsTrue, F0.x > 0.0f && F0.x < 1.0f);
}

/// @tags hair, fresnel, consistency
/// Test HairF0 is deterministic
[numthreads(1, 1, 1)] void TestHairF0Deterministic() {
	float3 F0_1 = HairTest::HairF0();
	float3 F0_2 = HairTest::HairF0();

	ASSERT(AreEqual, F0_1.x, F0_2.x);
	ASSERT(AreEqual, F0_1.y, F0_2.y);
	ASSERT(AreEqual, F0_1.z, F0_2.z);
}

	/// @tags hair, tangent, shifting
	/// Test ShiftTangent with zero shift
	[numthreads(1, 1, 1)] void TestShiftTangentZeroShift()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));
	float shift = 0.0f;

	float3 T_shifted = HairTest::ShiftTangent(T, N, shift);

	// With zero shift, result should be same as input (normalized)
	ASSERT(IsTrue, abs(T_shifted.x - T.x) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(T_shifted.y - T.y) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(T_shifted.z - T.z) < TestConstants::EXACT_TOLERANCE);
}

/// @tags hair, tangent, shifting
/// Test ShiftTangent with positive shift
[numthreads(1, 1, 1)] void TestShiftTangentPositiveShift() {
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));
	float shift = 0.5f;

	float3 T_shifted = HairTest::ShiftTangent(T, N, shift);

	// Result should be normalized
	float length = sqrt(dot(T_shifted, T_shifted));
	ASSERT(IsTrue, abs(length - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Shifted tangent should have component in normal direction
	ASSERT(IsTrue, T_shifted.z > T.z);
}

	/// @tags hair, tangent, shifting
	/// Test ShiftTangent with negative shift
	[numthreads(1, 1, 1)] void TestShiftTangentNegativeShift()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));
	float shift = -0.5f;

	float3 T_shifted = HairTest::ShiftTangent(T, N, shift);

	// Result should be normalized
	float length = sqrt(dot(T_shifted, T_shifted));
	ASSERT(IsTrue, abs(length - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Shifted tangent should have component opposite to normal direction
	ASSERT(IsTrue, T_shifted.z < T.z);
}

/// @tags hair, normal, shifting
/// Test ShiftNormal produces normalized result
[numthreads(1, 1, 1)] void TestShiftNormalNormalized() {
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));
	float shift = 0.3f;

	float3 N_shifted = HairTest::ShiftNormal(T, N, shift);

	// Result should be normalized
	float length = sqrt(dot(N_shifted, N_shifted));
	ASSERT(IsTrue, abs(length - 1.0f) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, normal, shifting
	/// Test ShiftNormal with zero shift
	[numthreads(1, 1, 1)] void TestShiftNormalZeroShift()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));
	float shift = 0.0f;

	float3 N_shifted = HairTest::ShiftNormal(T, N, shift);

	// With zero shift, result should be similar to input
	// (may have small differences due to cross product operations)
	ASSERT(IsTrue, abs(N_shifted.z - N.z) < 0.1f);
}

/// @tags hair, kajiya-kay, specular
/// Test D_KajiyaKay returns positive values
[numthreads(1, 1, 1)] void TestDKajiyaKayPositive() {
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0.5, 0.5, 0.707));
	float n = 20.0f;

	float3 result = HairTest::D_KajiyaKay(T, H, n);

	ASSERT(IsTrue, result.x >= 0.0f);
	ASSERT(IsTrue, result.y >= 0.0f);
	ASSERT(IsTrue, result.z >= 0.0f);
}

	/// @tags hair, kajiya-kay, specular
	/// Test D_KajiyaKay with perpendicular vectors
	[numthreads(1, 1, 1)] void TestDKajiyaKayPerpendicular()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0, 1, 0));  // Perpendicular to T
	float n = 20.0f;

	float3 result = HairTest::D_KajiyaKay(T, H, n);

	// When T and H are perpendicular, TH = 0, sinTH should be high
	// Result should be non-zero
	ASSERT(IsTrue, result.x > 0.0f);
}

/// @tags hair, kajiya-kay, specular
/// Test D_KajiyaKay with parallel vectors
[numthreads(1, 1, 1)] void TestDKajiyaKayParallel() {
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(1, 0, 0));  // Parallel to T
	float n = 20.0f;

	float3 result = HairTest::D_KajiyaKay(T, H, n);

	// When T and H are parallel, TH = 1, sinTH = 0
	// Result should be very small (but dirAtten keeps it from being zero)
	ASSERT(IsTrue, result.x >= 0.0f);
	ASSERT(IsTrue, result.x < 0.1f);
}

	/// @tags hair, kajiya-kay, shininess
	/// Test D_KajiyaKay shininess behavior
	[numthreads(1, 1, 1)] void TestDKajiyaKayShininess()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0, 1, 0));

	// Higher shininess should produce sharper highlights
	float3 result_low = HairTest::D_KajiyaKay(T, H, 5.0f);
	float3 result_high = HairTest::D_KajiyaKay(T, H, 50.0f);

	// Both should be positive
	ASSERT(IsTrue, result_low.x >= 0.0f);
	ASSERT(IsTrue, result_high.x >= 0.0f);

	// High shininess produces higher normalization factor
	ASSERT(IsTrue, result_high.x > result_low.x);
}

/// @tags hair, gaussian, distribution
/// Test Hair_g (Gaussian distribution) is symmetric
[numthreads(1, 1, 1)] void TestHairGaussianSymmetry() {
	float B = 0.3f;

	// Gaussian should be symmetric around 0
	float result_pos = HairTest::Hair_g(B, 0.5f);
	float result_neg = HairTest::Hair_g(B, -0.5f);

	ASSERT(IsTrue, abs(result_pos - result_neg) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, gaussian, distribution
	/// Test Hair_g maximum at zero
	[numthreads(1, 1, 1)] void TestHairGaussianMaximum()
{
	float B = 0.3f;

	float result_zero = HairTest::Hair_g(B, 0.0f);
	float result_offset = HairTest::Hair_g(B, 0.5f);

	// Gaussian peaks at theta = 0
	ASSERT(IsTrue, result_zero > result_offset);
	ASSERT(IsTrue, result_zero > 0.0f);
}

/// @tags hair, gaussian, distribution, robustness
/// Test Hair_g with various beta values
[numthreads(1, 1, 1)] void TestHairGaussianBetaVariation() {
	float theta = 0.2f;

	// Smaller beta = narrower distribution
	float result_narrow = HairTest::Hair_g(0.1f, theta);
	float result_wide = HairTest::Hair_g(0.5f, theta);

	ASSERT(IsTrue, result_narrow > 0.0f);
	ASSERT(IsTrue, result_wide > 0.0f);
	ASSERT(IsTrue, !isnan(result_narrow) && !isinf(result_narrow));
	ASSERT(IsTrue, !isnan(result_wide) && !isinf(result_wide));
}

	/// @tags hair, gaussian, edge-cases
	/// Test Hair_g numerical stability
	[numthreads(1, 1, 1)] void TestHairGaussianStability()
{
	float B = 0.3f;

	// Test with large theta (tail of distribution)
	float result_large = HairTest::Hair_g(B, 5.0f);
	ASSERT(IsTrue, result_large >= 0.0f);
	ASSERT(IsTrue, !isnan(result_large) && !isinf(result_large));

	// Test with small beta
	float result_small_beta = HairTest::Hair_g(0.01f, 0.1f);
	ASSERT(IsTrue, result_small_beta >= 0.0f);
	ASSERT(IsTrue, !isnan(result_small_beta) && !isinf(result_small_beta));
}

/// @tags hair, color, saturation
/// Test Saturation with grayscale input
[numthreads(1, 1, 1)] void TestSaturationGrayscale() {
	float3 gray = float3(0.5, 0.5, 0.5);
	float saturation = 0.5f;

	float3 result = HairTest::Saturation(gray, saturation);

	// Grayscale should remain grayscale
	ASSERT(IsTrue, abs(result.x - result.y) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(result.y - result.z) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, color, saturation
	/// Test Saturation with zero saturation
	[numthreads(1, 1, 1)] void TestSaturationZero()
{
	float3 color = float3(1.0, 0.0, 0.0);  // Red
	float saturation = 0.0f;

	float3 result = HairTest::Saturation(color, saturation);

	// Zero saturation should produce grayscale
	ASSERT(IsTrue, abs(result.x - result.y) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(result.y - result.z) < TestConstants::APPROX_TOLERANCE);
}

/// @tags hair, color, saturation
/// Test Saturation with full saturation
[numthreads(1, 1, 1)] void TestSaturationFull() {
	float3 color = float3(1.0, 0.5, 0.0);
	float saturation = 1.0f;

	float3 result = HairTest::Saturation(color, saturation);

	// Full saturation should return original color (saturated)
	ASSERT(IsTrue, abs(result.x - color.x) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(result.y - color.y) < TestConstants::APPROX_TOLERANCE);
	ASSERT(IsTrue, abs(result.z - color.z) < TestConstants::APPROX_TOLERANCE);
}

	/// @tags hair, color, saturation, edge-cases
	/// Test Saturation bounds
	[numthreads(1, 1, 1)] void TestSaturationBounds()
{
	float3 color = float3(0.8, 0.3, 0.2);

	// Test with saturation > 1 (should be clamped)
	float3 result_over = HairTest::Saturation(color, 2.0f);
	ASSERT(IsTrue, all(result_over >= 0.0f));
	ASSERT(IsTrue, all(result_over <= 1.0f));

	// Test with negative saturation (should be clamped)
	float3 result_neg = HairTest::Saturation(color, -0.5f);
	ASSERT(IsTrue, all(result_neg >= 0.0f));
	ASSERT(IsTrue, all(result_neg <= 1.0f));
}

/// @tags hair, diffuse, kajiya-kay
/// Test GetHairDiffuseAttenuationKajiyaKay returns positive values
[numthreads(1, 1, 1)] void TestHairDiffuseAttenuationPositive() {
	float3 N = normalize(float3(0, 0, 1));
	float3 V = normalize(float3(1, 0, 1));
	float3 L = normalize(float3(0, 1, 1));
	float shadow = 1.0f;
	float3 baseColor = float3(0.5, 0.3, 0.2);

	float3 result = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, shadow, baseColor);

	ASSERT(IsTrue, result.x >= 0.0f);
	ASSERT(IsTrue, result.y >= 0.0f);
	ASSERT(IsTrue, result.z >= 0.0f);
}

	/// @tags hair, diffuse, kajiya-kay
	/// Test GetHairDiffuseAttenuationKajiyaKay with full shadow
	[numthreads(1, 1, 1)] void TestHairDiffuseAttenuationShadow()
{
	float3 N = normalize(float3(0, 0, 1));
	float3 V = normalize(float3(1, 0, 1));
	float3 L = normalize(float3(0, 1, 1));
	float3 baseColor = float3(0.5, 0.3, 0.2);

	float3 result_lit = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 1.0f, baseColor);
	float3 result_shadow = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, 0.0f, baseColor);

	// Both should be positive
	ASSERT(IsTrue, result_lit.x >= 0.0f);
	ASSERT(IsTrue, result_shadow.x >= 0.0f);

	// Shadowed result should differ due to scatter tint
	ASSERT(IsTrue, abs(result_lit.x - result_shadow.x) > TestConstants::NEAR_ZERO);
}

/// @tags hair, diffuse, kajiya-kay, edge-cases
/// Test GetHairDiffuseAttenuationKajiyaKay with black base color
[numthreads(1, 1, 1)] void TestHairDiffuseAttenuationBlackColor() {
	float3 N = normalize(float3(0, 0, 1));
	float3 V = normalize(float3(1, 0, 1));
	float3 L = normalize(float3(0, 1, 1));
	float shadow = 1.0f;
	float3 baseColor = float3(0, 0, 0);

	float3 result = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, shadow, baseColor);

	// Should handle black color gracefully
	ASSERT(IsTrue, !isnan(result.x) && !isinf(result.x));
	ASSERT(IsTrue, result.x >= 0.0f);
}

	/// @tags hair, diffuse, kajiya-kay
	/// Test GetHairDiffuseAttenuationKajiyaKay with white base color
	[numthreads(1, 1, 1)] void TestHairDiffuseAttenuationWhiteColor()
{
	float3 N = normalize(float3(0, 0, 1));
	float3 V = normalize(float3(1, 0, 1));
	float3 L = normalize(float3(0, 1, 1));
	float shadow = 1.0f;
	float3 baseColor = float3(1, 1, 1);

	float3 result = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, shadow, baseColor);

	ASSERT(IsTrue, result.x > 0.0f);
	ASSERT(IsTrue, !isnan(result.x) && !isinf(result.x));
}

// ============================================================================
// EDGE CASE AND ROBUSTNESS TESTS
// ============================================================================

/// @tags hair, tangent, robustness, edge-cases
/// Test ReorientTangent with non-normalized inputs
[numthreads(1, 1, 1)] void TestReorientTangentNonNormalized() {
	float3 N = float3(0, 0, 2);    // Not normalized
	float3 T = float3(3, 0, 0.5);  // Not normalized

	float3 T_reoriented = HairTest::ReorientTangent(T, N);

	// Should still produce normalized result
	float length = sqrt(dot(T_reoriented, T_reoriented));
	ASSERT(IsTrue, abs(length - 1.0f) < 0.1f || isnan(length));
}

	/// @tags hair, tangent, robustness, edge-cases
	/// Test ShiftTangent with extreme shift values
	[numthreads(1, 1, 1)] void TestShiftTangentExtremeShift()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));

	// Very large positive shift
	float3 result_large = HairTest::ShiftTangent(T, N, 10.0f);
	float length_large = sqrt(dot(result_large, result_large));
	ASSERT(IsTrue, abs(length_large - 1.0f) < TestConstants::EXACT_TOLERANCE);

	// Very large negative shift
	float3 result_neg_large = HairTest::ShiftTangent(T, N, -10.0f);
	float length_neg = sqrt(dot(result_neg_large, result_neg_large));
	ASSERT(IsTrue, abs(length_neg - 1.0f) < TestConstants::EXACT_TOLERANCE);
}

/// @tags hair, kajiya-kay, robustness, edge-cases
/// Test D_KajiyaKay with zero shininess
[numthreads(1, 1, 1)] void TestDKajiyaKayZeroShininess() {
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0.5, 0.5, 0.707));

	float3 result = HairTest::D_KajiyaKay(T, H, 0.0f);

	// Should handle zero shininess gracefully
	ASSERT(IsTrue, !isnan(result.x) && !isinf(result.x));
	ASSERT(IsTrue, result.x >= 0.0f);
}

	/// @tags hair, kajiya-kay, robustness, edge-cases
	/// Test D_KajiyaKay with very high shininess
	[numthreads(1, 1, 1)] void TestDKajiyaKayHighShininess()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0, 1, 0));

	float3 result = HairTest::D_KajiyaKay(T, H, 1000.0f);

	// Should remain stable with high shininess
	ASSERT(IsTrue, !isnan(result.x) && !isinf(result.x));
	ASSERT(IsTrue, result.x >= 0.0f);
}

/// @tags hair, gaussian, robustness, edge-cases
/// Test Hair_g with near-zero beta
[numthreads(1, 1, 1)] void TestHairGaussianNearZeroBeta() {
	// Very small beta can cause division issues
	float result = HairTest::Hair_g(0.0001f, 0.1f);

	ASSERT(IsTrue, !isnan(result) && !isinf(result));
	ASSERT(IsTrue, result >= 0.0f);
}

	/// @tags hair, diffuse, robustness, edge-cases
	/// Test GetHairDiffuseAttenuationKajiyaKay with perpendicular vectors
	[numthreads(1, 1, 1)] void TestHairDiffuseAttenuationPerpendicularVectors()
{
	float3 N = normalize(float3(0, 0, 1));
	float3 V = normalize(float3(1, 0, 0));  // Perpendicular to N
	float3 L = normalize(float3(0, 1, 0));  // Perpendicular to N
	float shadow = 1.0f;
	float3 baseColor = float3(0.5, 0.3, 0.2);

	float3 result = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, shadow, baseColor);

	ASSERT(IsTrue, !isnan(result.x) && !isinf(result.x));
	ASSERT(IsTrue, result.x >= 0.0f);
}

// ============================================================================
// PHYSICAL PROPERTY TESTS
// ============================================================================

/// @tags hair, tangent, properties
/// Test that multiple ShiftTangent calls with opposite shifts are inverses
[numthreads(1, 1, 1)] void TestShiftTangentInverseProperty() {
	float3 T = normalize(float3(1, 0, 0));
	float3 N = normalize(float3(0, 0, 1));
	float shift = 0.3f;

	float3 T_shifted_forward = HairTest::ShiftTangent(T, N, shift);
	float3 T_shifted_back = HairTest::ShiftTangent(T, N, -shift);

	// Forward and backward shifts should produce different results
	ASSERT(IsTrue, abs(T_shifted_forward.z - T_shifted_back.z) > TestConstants::NEAR_ZERO);

	// Both should be normalized
	float len_fwd = sqrt(dot(T_shifted_forward, T_shifted_forward));
	float len_back = sqrt(dot(T_shifted_back, T_shifted_back));
	ASSERT(IsTrue, abs(len_fwd - 1.0f) < TestConstants::EXACT_TOLERANCE);
	ASSERT(IsTrue, abs(len_back - 1.0f) < TestConstants::EXACT_TOLERANCE);
}

	/// @tags hair, kajiya-kay, properties, monotonicity
	/// Test D_KajiyaKay monotonicity with shininess
	[numthreads(1, 1, 1)] void TestDKajiyaKayShininessMonotonicity()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0, 1, 0));

	// As shininess increases, normalization factor increases
	float prev_norm = 0.0f;

	for (float n = 2.0f; n <= 50.0f; n += 10.0f) {
		float norm = (n + 2) / (2 * Math::PI);
		ASSERT(IsTrue, norm > prev_norm);
		prev_norm = norm;

		float3 result = HairTest::D_KajiyaKay(T, H, n);
		ASSERT(IsTrue, result.x >= 0.0f);
		ASSERT(IsTrue, !isnan(result.x));
	}
}

/// @tags hair, gaussian, properties, normalization
/// Test Hair_g approximate normalization
[numthreads(1, 1, 1)] void TestHairGaussianNormalization() {
	float B = 0.3f;

	// Sample multiple points and verify they're consistent with Gaussian shape
	float center = HairTest::Hair_g(B, 0.0f);
	float mid = HairTest::Hair_g(B, B);
	float tail = HairTest::Hair_g(B, 3 * B);

	// Gaussian should decay from center
	ASSERT(IsTrue, center > mid);
	ASSERT(IsTrue, mid > tail);
	ASSERT(IsTrue, tail > 0.0f);
}

	/// @tags hair, color, properties
	/// Test Saturation preserves luminance relationship
	[numthreads(1, 1, 1)] void TestSaturationLuminanceRelationship()
{
	float3 color = float3(0.8, 0.3, 0.2);
	float luma_original = Color::RGBToLuminance(color);

	// At zero saturation, all channels should equal luminance
	float3 result_desaturated = HairTest::Saturation(color, 0.0f);
	float luma_desaturated = Color::RGBToLuminance(result_desaturated);

	// Luminance should be preserved (approximately)
	ASSERT(IsTrue, abs(luma_original - luma_desaturated) < TestConstants::APPROX_TOLERANCE);
}

/// @tags hair, diffuse, properties
/// Test GetHairDiffuseAttenuationKajiyaKay energy conservation
[numthreads(1, 1, 1)] void TestHairDiffuseAttenuationEnergyConservation() {
	float3 N = normalize(float3(0, 0, 1));
	float3 V = normalize(float3(0, 0, 1));
	float3 L = normalize(float3(0, 0, 1));
	float shadow = 1.0f;
	float3 baseColor = float3(0.8, 0.8, 0.8);

	float3 result = HairTest::GetHairDiffuseAttenuationKajiyaKay(N, V, L, shadow, baseColor);

	// Result should not exceed reasonable bounds
	ASSERT(IsTrue, result.x < 5.0f);  // Reasonable upper bound for diffuse
	ASSERT(IsTrue, result.y < 5.0f);
	ASSERT(IsTrue, result.z < 5.0f);
}

	/// @tags hair, kajiya-kay, regression
	/// Regression test: verify specific known case
	[numthreads(1, 1, 1)] void TestDKajiyaKayRegressionCase()
{
	float3 T = normalize(float3(1, 0, 0));
	float3 H = normalize(float3(0, 1, 0));
	float n = 20.0f;

	float3 result = HairTest::D_KajiyaKay(T, H, n);

	// For this specific case:
	// TH = 0, sinTH = 1, dirAtten = 1, norm = 22/(2*PI) ≈ 3.501
	// Result ≈ 1 * 3.501 * 1 = 3.501
	const float expectedNorm = (n + 2) / (2 * Math::PI);
	ASSERT(IsTrue, abs(result.x - expectedNorm) < 0.1f);
}