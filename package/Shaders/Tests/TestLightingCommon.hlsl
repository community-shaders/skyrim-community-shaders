// HLSL Unit Tests for Common/LightingCommon.hlsli
#include "/Shaders/Common/LightingCommon.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags lighting, material
[numthreads(1, 1, 1)]
void TestShininessToRoughness()
{
    // Test 1: Known conversions
    // Formula: roughness = (2/(shininess+2))^0.25
    // Shininess = 2: roughness = (2/4)^0.25 = 0.5^0.25 ≈ 0.841
    float roughness_low_shininess = ShininessToRoughness(2.0f);
    ASSERT(IsTrue, abs(roughness_low_shininess - 0.841f) < 0.01f);

    // Test 2: Higher shininess = lower roughness
    float shininess_low = 10.0f;
    float shininess_high = 100.0f;

    float roughness_low = ShininessToRoughness(shininess_low);
    float roughness_high = ShininessToRoughness(shininess_high);

    ASSERT(IsTrue, roughness_low > roughness_high);

    // Test 3: Result should be in valid range [0, 1]
    float testShininess[5] = { 2.0f, 10.0f, 50.0f, 200.0f, 1000.0f };

    for (int i = 0; i < 5; i++)
    {
        float r = ShininessToRoughness(testShininess[i]);
        ASSERT(IsTrue, r >= 0.0f);
        ASSERT(IsTrue, r <= 1.0f);
    }

    // Test 4: Very high shininess (mirror-like) should give low roughness
    // shininess = 10000: roughness = (2/10002)^0.25 ≈ 0.376
    float roughness_mirror = ShininessToRoughness(10000.0f);
    ASSERT(IsTrue, roughness_mirror < 0.4f);

    // Test 5: Monotonicity - increasing shininess should decrease roughness
    float r1 = ShininessToRoughness(10.0f);
    float r2 = ShininessToRoughness(20.0f);
    float r3 = ShininessToRoughness(40.0f);

    ASSERT(IsTrue, r1 > r2);
    ASSERT(IsTrue, r2 > r3);

    // Test 6: Formula verification - roughness = (2/(shininess+2))^0.25
    float shininess = 50.0f;
    float expected = pow(2.0f / (shininess + 2.0f), 0.25f);
    float actual = ShininessToRoughness(shininess);
    ASSERT(IsTrue, abs(actual - expected) < 0.0001f);
}

/// @tags lighting, material, edge-cases
[numthreads(1, 1, 1)]
void TestShininessToRoughnessEdgeCases()
{
    // Test near-zero shininess
    float roughness_zero = ShininessToRoughness(0.0f);
    ASSERT(IsTrue, !isnan(roughness_zero));
    ASSERT(IsTrue, !isinf(roughness_zero));
    ASSERT(IsTrue, roughness_zero >= 0.0f && roughness_zero <= 1.0f);

    // Zero shininess: (2/2)^0.25 = 1.0
    ASSERT(IsTrue, abs(roughness_zero - 1.0f) < 0.01f);

    // Test very small shininess
    float roughness_small = ShininessToRoughness(0.1f);
    ASSERT(IsTrue, !isnan(roughness_small));
    ASSERT(IsTrue, roughness_small > 0.9f); // Should be very rough

    // Test very large shininess
    float roughness_large = ShininessToRoughness(100000.0f);
    ASSERT(IsTrue, !isnan(roughness_large));
    ASSERT(IsTrue, roughness_large < 0.4f); // Should be very smooth

    // Test negative shininess (using abs in formula)
    float roughness_neg = ShininessToRoughness(-10.0f);
    ASSERT(IsTrue, !isnan(roughness_neg));
    ASSERT(IsTrue, !isinf(roughness_neg));
}

/// @tags lighting, material, properties
[numthreads(1, 1, 1)]
void TestShininessToRoughnessProperties()
{
    // Property: Continuous and smooth
    float prev = ShininessToRoughness(1.0f);

    for (float s = 2.0f; s < 100.0f; s += 10.0f)
    {
        float curr = ShininessToRoughness(s);

        // Should be monotonically decreasing
        ASSERT(IsTrue, curr < prev);

        // Should not have huge jumps (smoothness)
        float diff = abs(curr - prev);
        ASSERT(IsTrue, diff < 0.5f);

        prev = curr;
    }

    // Property: Bounded output
    for (float s = 0.1f; s < 1000.0f; s *= 2.0f)
    {
        float r = ShininessToRoughness(s);
        ASSERT(IsTrue, r >= 0.0f);
        ASSERT(IsTrue, r <= 1.0f);
    }
}
