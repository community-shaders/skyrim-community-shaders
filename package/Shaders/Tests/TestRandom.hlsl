// HLSL Unit Tests for Common/Random.hlsli
#include "/Shaders/Common/Random.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags random, pcg, rng
[numthreads(1, 1, 1)]
void TestPCGBasicProperties()
{
    uint state = 12345u;

    // Generate a few random numbers
    uint r1 = Random::pcg(state);
    uint r2 = Random::pcg(state);
    uint r3 = Random::pcg(state);

    // PCG should produce deterministic results for same seed
    uint state2 = 12345u;
    uint check = Random::pcg(state2);
    ASSERT(AreEqual, r1, check);  // Same seed should give same first value

    // NOTE: We don't check r1 != r2 because hash collisions are theoretically possible
    // Instead verify probabilistic properties: at least one non-zero value
    ASSERT(IsTrue, r1 != 0u || r2 != 0u || r3 != 0u);

    // Verify state advances (deterministic property of PCG)
    ASSERT(IsTrue, state != 12345u);  // State should have changed after calls
}

/// @tags random, pcg, rng
[numthreads(1, 1, 1)]
void TestPCGDeterministic()
{
    // Same seed should produce same sequence
    uint state1 = 42u;
    uint state2 = 42u;

    uint r1a = Random::pcg(state1);
    uint r1b = Random::pcg(state2);
    ASSERT(AreEqual, r1a, r1b);

    uint r2a = Random::pcg(state1);
    uint r2b = Random::pcg(state2);
    ASSERT(AreEqual, r2a, r2b);
}

/// @tags random, rng
[numthreads(1, 1, 1)]
void TestF1Range()
{
    uint state = 98765u;

    float minVal = 1.0f;
    float maxVal = 0.0f;
    bool hasVariety = false;
    float firstVal = Random::f1(state);

    // Generate several random floats and track statistics
    for (int i = 0; i < 10; i++)
    {
        float r = Random::f1(state);

        // Test 1: Should be in range [0, 1)
        ASSERT(IsTrue, r >= 0.0f);
        ASSERT(IsTrue, r < 1.0f);

        // Track min/max
        minVal = min(minVal, r);
        maxVal = max(maxVal, r);

        // Check for variety (not all same value)
        if (abs(r - firstVal) > 0.01f) {
            hasVariety = true;
        }
    }

    // Test 2: Should produce varied values (not constant)
    ASSERT(IsTrue, hasVariety);

    // Test 3: Should explore reasonable range
    ASSERT(IsTrue, (maxVal - minVal) > 0.1f);
}

/// @tags random, rng
[numthreads(1, 1, 1)]
void TestF2Range()
{
    uint state = 55555u;

    // Generate several random float2s
    for (int i = 0; i < 5; i++)
    {
        float2 r = Random::f2(state);

        // Both components should be in range [0, 1)
        ASSERT(IsTrue, r.x >= 0.0f && r.x < 1.0f);
        ASSERT(IsTrue, r.y >= 0.0f && r.y < 1.0f);

        // Components should generally be different
        // (could rarely be equal, but extremely unlikely)
        if (i > 0)
        {
            ASSERT(IsTrue, abs(r.x - r.y) > 0.000001f);
        }
    }
}

/// @tags random, rng
[numthreads(1, 1, 1)]
void TestF3Range()
{
    uint state = 77777u;

    float3 r = Random::f3(state);

    // All components should be in range [0, 1)
    ASSERT(IsTrue, r.x >= 0.0f && r.x < 1.0f);
    ASSERT(IsTrue, r.y >= 0.0f && r.y < 1.0f);
    ASSERT(IsTrue, r.z >= 0.0f && r.z < 1.0f);
}

/// @tags random, pcg, hash
[numthreads(1, 1, 1)]
void TestPCG2D()
{
    uint2 v1 = uint2(123, 456);
    uint2 v2 = uint2(123, 456);
    uint2 v3 = uint2(789, 101);

    uint2 r1 = Random::pcg2d(v1);
    uint2 r2 = Random::pcg2d(v2);
    uint2 r3 = Random::pcg2d(v3);

    // Same input produces same output (deterministic)
    ASSERT(AreEqual, r1.x, r2.x);
    ASSERT(AreEqual, r1.y, r2.y);

    // Different inputs produce different outputs
    ASSERT(IsTrue, r1.x != r3.x || r1.y != r3.y);
}

/// @tags random, pcg, hash
[numthreads(1, 1, 1)]
void TestPCG3D()
{
    uint3 v1 = uint3(111, 222, 333);
    uint3 v2 = uint3(111, 222, 333);

    uint3 r1 = Random::pcg3d(v1);
    uint3 r2 = Random::pcg3d(v2);

    // Same input produces same output
    ASSERT(AreEqual, r1.x, r2.x);
    ASSERT(AreEqual, r1.y, r2.y);
    ASSERT(AreEqual, r1.z, r2.z);

    // Should produce non-zero values
    ASSERT(IsTrue, r1.x != 0u || r1.y != 0u || r1.z != 0u);
}

/// @tags random, noise
[numthreads(1, 1, 1)]
void TestInterleavedGradientNoise()
{
    float2 coord1 = float2(10.5, 20.3);
    float2 coord2 = float2(10.5, 20.3);
    float2 coord3 = float2(11.5, 21.3);

    float noise1 = Random::InterleavedGradientNoise(coord1);
    float noise2 = Random::InterleavedGradientNoise(coord2);
    float noise3 = Random::InterleavedGradientNoise(coord3);

    // Same coordinates produce same noise (deterministic)
    ASSERT(AreEqual, noise1, noise2);

    // Different coordinates produce different noise
    ASSERT(IsTrue, abs(noise1 - noise3) > 0.001f);

    // Should be in range [0, 1)
    ASSERT(IsTrue, noise1 >= 0.0f);
    ASSERT(IsTrue, noise1 < 1.0f);
    ASSERT(IsTrue, noise3 >= 0.0f);
    ASSERT(IsTrue, noise3 < 1.0f);
}

/// @tags random, quasirandom, sequence
[numthreads(1, 1, 1)]
void TestR1Sequence()
{
    // R1 sequence should produce values in [0, 1)
    float r0 = Random::R1Sequence(0.0f);
    float r1 = Random::R1Sequence(1.0f);
    float r2 = Random::R1Sequence(2.0f);

    ASSERT(IsTrue, r0 >= 0.0f && r0 < 1.0f);
    ASSERT(IsTrue, r1 >= 0.0f && r1 < 1.0f);
    ASSERT(IsTrue, r2 >= 0.0f && r2 < 1.0f);

    // Sequential values should be different
    ASSERT(IsTrue, abs(r0 - r1) > 0.001f);
    ASSERT(IsTrue, abs(r1 - r2) > 0.001f);
}

/// @tags random, quasirandom, sequence
[numthreads(1, 1, 1)]
void TestR2Sequence()
{
    float2 r0 = Random::R2Sequence(0.0f);
    float2 r1 = Random::R2Sequence(1.0f);

    // Both components should be in [0, 1)
    ASSERT(IsTrue, r0.x >= 0.0f && r0.x < 1.0f);
    ASSERT(IsTrue, r0.y >= 0.0f && r0.y < 1.0f);
    ASSERT(IsTrue, r1.x >= 0.0f && r1.x < 1.0f);
    ASSERT(IsTrue, r1.y >= 0.0f && r1.y < 1.0f);

    // Sequential samples should differ
    ASSERT(IsTrue, abs(r0.x - r1.x) > 0.001f || abs(r0.y - r1.y) > 0.001f);
}

/// @tags random, quasirandom, sequence
[numthreads(1, 1, 1)]
void TestR3Sequence()
{
    float3 r = Random::R3Sequence(5.0f);

    // All components in [0, 1)
    ASSERT(IsTrue, r.x >= 0.0f && r.x < 1.0f);
    ASSERT(IsTrue, r.y >= 0.0f && r.y < 1.0f);
    ASSERT(IsTrue, r.z >= 0.0f && r.z < 1.0f);
}

/// @tags random, hash
[numthreads(1, 1, 1)]
void TestMurmur3Hash()
{
    uint3 input1 = uint3(1, 2, 3);
    uint3 input2 = uint3(1, 2, 3);
    uint3 input3 = uint3(3, 2, 1);

    uint hash1 = Random::murmur3(input1);
    uint hash2 = Random::murmur3(input2);
    uint hash3 = Random::murmur3(input3);

    // Same input produces same hash (deterministic)
    ASSERT(AreEqual, hash1, hash2);

    // Different inputs SHOULD produce different hashes (not guaranteed due to hash collisions)
    // But for our test inputs, they should differ
    // NOTE: This could theoretically fail for some input pairs, but these specific values don't collide
    ASSERT(IsTrue, hash1 != hash3);

    // Should produce non-zero values
    ASSERT(IsTrue, hash1 != 0u);
}

/// @tags random, noise, perlin
[numthreads(1, 1, 1)]
void TestPerlinNoiseRange()
{
    // Test perlin noise at a few positions
    float noise1 = Random::perlinNoise(float3(1.5, 2.3, 3.7));
    float noise2 = Random::perlinNoise(float3(10.2, 5.8, 7.1));

    // Perlin noise should be in range [-1, 1]
    ASSERT(IsTrue, noise1 >= -1.0f);
    ASSERT(IsTrue, noise1 <= 1.0f);
    ASSERT(IsTrue, noise2 >= -1.0f);
    ASSERT(IsTrue, noise2 <= 1.0f);
}

/// @tags random, noise, perlin
[numthreads(1, 1, 1)]
void TestPerlinNoiseContinuity()
{
    float3 pos = float3(5.0, 5.0, 5.0);

    // Sample noise at position and nearby
    float noise1 = Random::perlinNoise(pos);
    float noise2 = Random::perlinNoise(pos + float3(0.01, 0.0, 0.0));

    // Nearby positions should have similar values (continuity)
    ASSERT(IsTrue, abs(noise1 - noise2) < 0.5f);
}
