// Tests for Common/Random.hlsli
#include "test_helpers_unified.h"

// PCG Basic Properties
SHADER_TEST_SIMPLE("Random - PCG Basic Properties", "[random][pcg]",
	"/Shaders/Tests/TestRandom.hlsl", "TestPCGBasicProperties")

// PCG Deterministic
SHADER_TEST_SIMPLE("Random - PCG Deterministic", "[random][pcg]",
	"/Shaders/Tests/TestRandom.hlsl", "TestPCGDeterministic")

// f1 Range
SHADER_TEST_SIMPLE("Random - f1 Range", "[random][float]",
	"/Shaders/Tests/TestRandom.hlsl", "TestF1Range")

// f2 Range
SHADER_TEST_SIMPLE("Random - f2 Range", "[random][float]",
	"/Shaders/Tests/TestRandom.hlsl", "TestF2Range")

// f3 Range
SHADER_TEST_SIMPLE("Random - f3 Range", "[random][float]",
	"/Shaders/Tests/TestRandom.hlsl", "TestF3Range")

// PCG 2D
SHADER_TEST_SIMPLE("Random - PCG 2D", "[random][pcg]",
	"/Shaders/Tests/TestRandom.hlsl", "TestPCG2D")

// PCG 3D
SHADER_TEST_SIMPLE("Random - PCG 3D", "[random][pcg]",
	"/Shaders/Tests/TestRandom.hlsl", "TestPCG3D")

// Interleaved Gradient Noise
SHADER_TEST_SIMPLE("Random - Interleaved Gradient Noise", "[random][noise]",
	"/Shaders/Tests/TestRandom.hlsl", "TestInterleavedGradientNoise")

// R1 Sequence
SHADER_TEST_SIMPLE("Random - R1 Sequence", "[random][quasirandom]",
	"/Shaders/Tests/TestRandom.hlsl", "TestR1Sequence")

// R2 Sequence
SHADER_TEST_SIMPLE("Random - R2 Sequence", "[random][quasirandom]",
	"/Shaders/Tests/TestRandom.hlsl", "TestR2Sequence")

// R3 Sequence
SHADER_TEST_SIMPLE("Random - R3 Sequence", "[random][quasirandom]",
	"/Shaders/Tests/TestRandom.hlsl", "TestR3Sequence")

// Murmur3 Hash
SHADER_TEST_SIMPLE("Random - Murmur3 Hash", "[random][hash]",
	"/Shaders/Tests/TestRandom.hlsl", "TestMurmur3Hash")

// Perlin Noise Range
SHADER_TEST_SIMPLE("Random - Perlin Noise Range", "[random][noise][perlin]",
	"/Shaders/Tests/TestRandom.hlsl", "TestPerlinNoiseRange")

// Perlin Noise Continuity
SHADER_TEST_SIMPLE("Random - Perlin Noise Continuity", "[random][noise][perlin]",
	"/Shaders/Tests/TestRandom.hlsl", "TestPerlinNoiseContinuity")
