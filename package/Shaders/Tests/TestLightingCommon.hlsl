// HLSL Unit Tests for Common/LightingCommon.hlsli
#include "/Shaders/Common/LightingCommon.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

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
