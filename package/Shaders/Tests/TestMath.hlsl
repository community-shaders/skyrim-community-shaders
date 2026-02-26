// HLSL Unit Tests for Common/Math.hlsli
#include "/Shaders/Common/Math.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags math, constants
[numthreads(1, 1, 1)] void TestMathConstants() {
	// Test PI is approximately 3.14159265359
	const float expectedPI = 3.14159265359f;
	ASSERT(IsTrue, abs(Math::PI - expectedPI) < 0.0001f);

	// Test HALF_PI is approximately PI/2
	const float expectedHalfPI = expectedPI * 0.5f;
	ASSERT(IsTrue, abs(Math::HALF_PI - expectedHalfPI) < 0.0001f);

	// Test TAU is approximately 2*PI
	const float expectedTAU = expectedPI * 2.0f;
	ASSERT(IsTrue, abs(Math::TAU - expectedTAU) < 0.0001f);

	// Test mathematical relationships
	ASSERT(AreEqual, Math::TAU, Math::PI * 2.0f);
	ASSERT(AreEqual, Math::HALF_PI, Math::PI * 0.5f);
	ASSERT(IsTrue, Math::TAU > Math::PI);
	ASSERT(IsTrue, Math::PI > Math::HALF_PI);
	ASSERT(IsTrue, Math::HALF_PI > 0.0f);
}

	/// @tags math, constants
	[numthreads(1, 1, 1)] void TestEpsilonConstants()
{
	// EPSILON_SSS_ALBEDO should be 1e-3f
	ASSERT(IsTrue, EPSILON_SSS_ALBEDO > 0.0f);
	ASSERT(IsTrue, EPSILON_SSS_ALBEDO < 0.01f);
	ASSERT(AreEqual, EPSILON_SSS_ALBEDO, 1e-3f);

	// EPSILON_DOT_CLAMP should be 1e-5f
	ASSERT(IsTrue, EPSILON_DOT_CLAMP > 0.0f);
	ASSERT(IsTrue, EPSILON_DOT_CLAMP < 0.0001f);
	ASSERT(AreEqual, EPSILON_DOT_CLAMP, 1e-5f);

	// EPSILON_DIVISION should be 1e-6f
	ASSERT(IsTrue, EPSILON_DIVISION > 0.0f);
	ASSERT(IsTrue, EPSILON_DIVISION < 0.00001f);
	ASSERT(AreEqual, EPSILON_DIVISION, 1e-6f);

	// Verify ordering: DIVISION < DOT_CLAMP < SSS_ALBEDO
	ASSERT(IsTrue, EPSILON_DIVISION < EPSILON_DOT_CLAMP);
	ASSERT(IsTrue, EPSILON_DOT_CLAMP < EPSILON_SSS_ALBEDO);
}

/// @tags math, matrix
[numthreads(1, 1, 1)] void TestIdentityMatrix() {
	// Test diagonal elements are 1.0
	ASSERT(AreEqual, Math::IdentityMatrix[0][0], 1.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[1][1], 1.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[2][2], 1.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[3][3], 1.0f);

	// Test off-diagonal elements are 0.0
	// Row 0
	ASSERT(AreEqual, Math::IdentityMatrix[0][1], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[0][2], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[0][3], 0.0f);

	// Row 1
	ASSERT(AreEqual, Math::IdentityMatrix[1][0], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[1][2], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[1][3], 0.0f);

	// Row 2
	ASSERT(AreEqual, Math::IdentityMatrix[2][0], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[2][1], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[2][3], 0.0f);

	// Row 3
	ASSERT(AreEqual, Math::IdentityMatrix[3][0], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[3][1], 0.0f);
	ASSERT(AreEqual, Math::IdentityMatrix[3][2], 0.0f);
}
