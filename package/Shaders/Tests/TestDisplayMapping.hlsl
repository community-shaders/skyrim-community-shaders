// HLSL Unit Tests for Common/DisplayMapping.hlsli

// Stubs for dependencies from ISHDR.hlsl (not tested here)
float3 GetTonemapFactorHejlBurgessDawson(float3 x) { return x; }
static const float4 Param = { 0, 0, 0, 0 };

#include "/Shaders/Common/DisplayMapping.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags hdr, tonemapping
[numthreads(1, 1, 1)]
void TestRangeCompressSingle()
{
    // RangeCompress(x) = 1 - exp(-x)

    // At x=0, should be 0
    float result_0 = DisplayMapping::RangeCompress(0.0f);
    ASSERT(IsTrue, abs(result_0) < 0.001f);

    // Should be monotonically increasing
    float result_1 = DisplayMapping::RangeCompress(1.0f);
    float result_2 = DisplayMapping::RangeCompress(2.0f);
    float result_5 = DisplayMapping::RangeCompress(5.0f);

    ASSERT(IsTrue, result_1 > result_0);
    ASSERT(IsTrue, result_2 > result_1);
    ASSERT(IsTrue, result_5 > result_2);

    // Should approach 1.0 asymptotically (never exceed)
    ASSERT(IsTrue, result_5 < 1.0f);

    // Large value should be close to 1
    float result_10 = DisplayMapping::RangeCompress(10.0f);
    ASSERT(IsTrue, result_10 > 0.99f);
    ASSERT(IsTrue, result_10 < 1.0f);
}

/// @tags hdr, tonemapping
[numthreads(1, 1, 1)]
void TestRangeCompressThreshold()
{
    float threshold = 0.5f;

    // Below threshold, should be identity
    float val_low = 0.3f;
    float result_low = DisplayMapping::RangeCompress(val_low, threshold);
    ASSERT(AreEqual, result_low, val_low);

    // At threshold, should be continuous
    float result_at = DisplayMapping::RangeCompress(threshold, threshold);
    ASSERT(IsTrue, abs(result_at - threshold) < 0.001f);

    // Above threshold, should compress
    float val_high = 0.8f;
    float result_high = DisplayMapping::RangeCompress(val_high, threshold);
    ASSERT(IsTrue, result_high < 1.0f);
    ASSERT(IsTrue, result_high >= threshold);
}

/// @tags hdr, tonemapping
[numthreads(1, 1, 1)]
void TestRangeCompressFloat3()
{
    float3 val = float3(0.3f, 0.6f, 0.9f);
    float threshold = 0.5f;

    float3 result = DisplayMapping::RangeCompress(val, threshold);

    // Each component should be processed independently
    ASSERT(AreEqual, result.x, val.x);  // Below threshold
    ASSERT(IsTrue, result.y > threshold);  // Above threshold
    ASSERT(IsTrue, result.z > threshold);  // Above threshold

    // Should be in valid range
    ASSERT(IsTrue, result.x >= 0.0f && result.x < 1.0f);
    ASSERT(IsTrue, result.y >= 0.0f && result.y < 1.0f);
    ASSERT(IsTrue, result.z >= 0.0f && result.z < 1.0f);
}

/// @tags hdr, pq, colorspace
[numthreads(1, 1, 1)]
void TestLinearToPQRoundtrip()
{
    float maxPQ = 100.0f;  // 100 nits

    // Test various brightness levels
    float3 colors[4] = {
        float3(0.1f, 0.1f, 0.1f),
        float3(0.5f, 0.5f, 0.5f),
        float3(1.0f, 1.0f, 1.0f),
        float3(0.3f, 0.7f, 0.9f)
    };

    for (int i = 0; i < 4; i++)
    {
        float3 original = colors[i];
        float3 pq = DisplayMapping::LinearToPQ(original, maxPQ);
        float3 roundtrip = DisplayMapping::PQtoLinear(pq, maxPQ);

        // Should roundtrip with small error
        ASSERT(IsTrue, abs(roundtrip.r - original.r) < 0.01f);
        ASSERT(IsTrue, abs(roundtrip.g - original.g) < 0.01f);
        ASSERT(IsTrue, abs(roundtrip.b - original.b) < 0.01f);
    }
}

/// @tags hdr, colorspace
[numthreads(1, 1, 1)]
void TestRGBToXYZRoundtrip()
{
    float3 colors[4] = {
        float3(1.0f, 0.0f, 0.0f),  // Red
        float3(0.0f, 1.0f, 0.0f),  // Green
        float3(0.0f, 0.0f, 1.0f),  // Blue
        float3(0.5f, 0.3f, 0.8f)   // Purple-ish
    };

    for (int i = 0; i < 4; i++)
    {
        float3 original = colors[i];
        float3 xyz = DisplayMapping::RGBToXYZ(original);
        float3 roundtrip = DisplayMapping::XYZToRGB(xyz);

        // Should roundtrip accurately
        ASSERT(IsTrue, abs(roundtrip.r - original.r) < 0.001f);
        ASSERT(IsTrue, abs(roundtrip.g - original.g) < 0.001f);
        ASSERT(IsTrue, abs(roundtrip.b - original.b) < 0.001f);
    }
}

/// @tags hdr, colorspace
[numthreads(1, 1, 1)]
void TestXYZToLMSRoundtrip()
{
    float3 xyzColors[3] = {
        float3(0.5f, 0.5f, 0.5f),
        float3(0.2f, 0.8f, 0.3f),
        float3(0.9f, 0.1f, 0.6f)
    };

    for (int i = 0; i < 3; i++)
    {
        float3 original = xyzColors[i];
        float3 lms = DisplayMapping::XYZToLMS(original);
        float3 roundtrip = DisplayMapping::LMSToXYZ(lms);

        // Should roundtrip accurately
        ASSERT(IsTrue, abs(roundtrip.x - original.x) < 0.001f);
        ASSERT(IsTrue, abs(roundtrip.y - original.y) < 0.001f);
        ASSERT(IsTrue, abs(roundtrip.z - original.z) < 0.001f);
    }
}

/// @tags hdr, colorspace, ictcp
[numthreads(1, 1, 1)]
void TestRGBToICtCpRoundtrip()
{
    // Test with moderate brightness colors (avoid very bright/dark for stability)
    float3 colors[3] = {
        float3(0.5f, 0.5f, 0.5f),  // Gray
        float3(0.3f, 0.2f, 0.4f),  // Dark purple
        float3(0.7f, 0.5f, 0.2f)   // Orange
    };

    for (int i = 0; i < 3; i++)
    {
        float3 original = colors[i];
        float3 ictcp = DisplayMapping::RGBToICtCp(original);
        float3 roundtrip = DisplayMapping::ICtCpToRGB(ictcp);

        // Should roundtrip with reasonable accuracy
        // ICtCp has PQ encoding so some error is expected
        ASSERT(IsTrue, abs(roundtrip.r - original.r) < 0.02f);
        ASSERT(IsTrue, abs(roundtrip.g - original.g) < 0.02f);
        ASSERT(IsTrue, abs(roundtrip.b - original.b) < 0.02f);
    }
}

/// @tags hdr, luminance, ictcp
[numthreads(1, 1, 1)]
void TestICtCpLuminance()
{
    // In ICtCp, the I channel represents intensity (luminance)

    // Brighter color should have higher I
    float3 dark = float3(0.2f, 0.2f, 0.2f);
    float3 bright = float3(0.8f, 0.8f, 0.8f);

    float3 ictcp_dark = DisplayMapping::RGBToICtCp(dark);
    float3 ictcp_bright = DisplayMapping::RGBToICtCp(bright);

    // I channel should increase with brightness
    ASSERT(IsTrue, ictcp_bright.x > ictcp_dark.x);
}

/// @tags hdr, pq
[numthreads(1, 1, 1)]
void TestPQConstants()
{
    // Verify PQ constants are in expected ranges
    // These are defined in the spec ST.2084

    ASSERT(IsTrue, DisplayMapping::PQ_constant_N > 0.0f);
    ASSERT(IsTrue, DisplayMapping::PQ_constant_M > 0.0f);
    ASSERT(IsTrue, DisplayMapping::PQ_constant_C1 > 0.0f);
    ASSERT(IsTrue, DisplayMapping::PQ_constant_C2 > 0.0f);
    ASSERT(IsTrue, DisplayMapping::PQ_constant_C3 > 0.0f);

    // N should be around 0.159 (from spec)
    ASSERT(IsTrue, DisplayMapping::PQ_constant_N > 0.15f);
    ASSERT(IsTrue, DisplayMapping::PQ_constant_N < 0.17f);
}

/// @tags hdr, colorspace
[numthreads(1, 1, 1)]
void TestRGBToXYZWhitePoint()
{
    // D65 white point (1,1,1) in RGB should convert to approximately (0.95, 1.0, 1.09) in XYZ
    float3 white = float3(1.0f, 1.0f, 1.0f);
    float3 xyz = DisplayMapping::RGBToXYZ(white);

    // Y component should be 1.0 (normalized)
    ASSERT(IsTrue, abs(xyz.y - 1.0f) < 0.01f);

    // X should be around 0.95
    ASSERT(IsTrue, xyz.x > 0.90f && xyz.x < 1.0f);

    // Z should be around 1.09
    ASSERT(IsTrue, xyz.z > 1.0f && xyz.z < 1.15f);
}

/// @tags hdr, colorspace
[numthreads(1, 1, 1)]
void TestRGBToXYZBlack()
{
    // Black should map to black in all color spaces
    float3 black = float3(0.0f, 0.0f, 0.0f);

    float3 xyz = DisplayMapping::RGBToXYZ(black);
    ASSERT(IsTrue, abs(xyz.x) < 0.001f);
    ASSERT(IsTrue, abs(xyz.y) < 0.001f);
    ASSERT(IsTrue, abs(xyz.z) < 0.001f);

    float3 lms = DisplayMapping::XYZToLMS(xyz);
    ASSERT(IsTrue, abs(lms.x) < 0.001f);
    ASSERT(IsTrue, abs(lms.y) < 0.001f);
    ASSERT(IsTrue, abs(lms.z) < 0.001f);
}
