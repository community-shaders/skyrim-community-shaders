// Tests for Common/Math.hlsli
#include "test_helpers_unified.h"

// Test mathematical constants (PI, TAU, HALF_PI)
SHADER_TEST_SIMPLE("Math.hlsli - Constants", "[math][constants]",
	"/Shaders/Tests/TestMath.hlsl", "TestMathConstants")

// Test epsilon values used for numerical stability
SHADER_TEST_SIMPLE("Math.hlsli - Epsilon Constants", "[math][epsilon]",
	"/Shaders/Tests/TestMath.hlsl", "TestEpsilonConstants")

// Test identity matrix correctness
SHADER_TEST_SIMPLE("Math.hlsli - Identity Matrix", "[math][matrix]",
	"/Shaders/Tests/TestMath.hlsl", "TestIdentityMatrix")
