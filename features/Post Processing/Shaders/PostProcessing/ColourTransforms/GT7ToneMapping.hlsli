//
// GT7 Tone Mapping - HLSL Compute Shader Implementation
// Based on Polyphony Digital Inc.'s GT7 Tone Mapping operator
//
// Version: 1.0 (2025-08-19) - HLSL Conversion
// Original Version: 1.0 (2025-08-10) - C++ Implementation
//

#ifndef GT7_TONE_MAPPING_HLSLI
#define GT7_TONE_MAPPING_HLSLI

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
#define TONE_MAPPING_UCS_ICTCP  0
#define TONE_MAPPING_UCS_JZAZBZ 1
#define TONE_MAPPING_UCS        TONE_MAPPING_UCS_ICTCP

#define GRAN_TURISMO_SDR_PAPER_WHITE 250.0f // cd/m^2
#define REFERENCE_LUMINANCE 100.0f // cd/m^2 <-> 1.0f
#define JZAZBZ_EXPONENT_SCALE_FACTOR 1.7f

// -----------------------------------------------------------------------------
// Luminance conversion helpers
// -----------------------------------------------------------------------------
float frameBufferValueToPhysicalValue(float fbValue)
{
    return fbValue * REFERENCE_LUMINANCE;
}

float physicalValueToFrameBufferValue(float physical)
{
    return physical / REFERENCE_LUMINANCE;
}

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------
float smoothStep(float x, float edge0, float edge1)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = saturate(t);
    return t * t * (3.0f - 2.0f * t);
}

float chromaCurve(float x, float a, float b)
{
    return 1.0f - smoothStep(x, a, b);
}

// -----------------------------------------------------------------------------
// GT Tone Mapping Curve Structure
// -----------------------------------------------------------------------------
struct GTToneMappingCurveV2
{
    float peakIntensity;
    float alpha;
    float midPoint;
    float linearSection;
    float toeStrength;
    float kA, kB, kC;
};

GTToneMappingCurveV2 initializeCurve(float monitorIntensity, float alpha, 
                                    float grayPoint, float linearSection, float toeStrength)
{
    GTToneMappingCurveV2 curve;
    curve.peakIntensity = monitorIntensity;
    curve.alpha = alpha;
    curve.midPoint = grayPoint;
    curve.linearSection = linearSection;
    curve.toeStrength = toeStrength;

    // Pre-compute constants for the shoulder region
    float k = (linearSection - 1.0f) / (alpha - 1.0f);
    curve.kA = monitorIntensity * linearSection + monitorIntensity * k;
    curve.kB = -monitorIntensity * k * exp(linearSection / k);
    curve.kC = -1.0f / (k * monitorIntensity);
    
    return curve;
}

float evaluateCurve(GTToneMappingCurveV2 curve, float x)
{
    if (x < 0.0f) return 0.0f;

    float weightLinear = smoothStep(x, 0.0f, curve.midPoint);
    float weightToe = 1.0f - weightLinear;

    // Shoulder mapping for highlights
    float shoulder = curve.kA + curve.kB * exp(x * curve.kC);

    if (x < curve.linearSection * curve.peakIntensity)
    {
        float toeMapped = curve.midPoint * pow(x / curve.midPoint, curve.toeStrength);
        return weightToe * toeMapped + weightLinear * x;
    }
    else
    {
        return shoulder;
    }
}

// -----------------------------------------------------------------------------
// ST-2084 (PQ) EOTF Functions
// -----------------------------------------------------------------------------
float eotfSt2084(float n, float exponentScaleFactor = 1.0f)
{
    n = saturate(n);

    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f * exponentScaleFactor;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    const float pqC = 10000.0f;

    float np = pow(n, 1.0f / m2);
    float l = max(np - c1, 0.0f);
    l = l / (c2 - c3 * np);
    l = pow(l, 1.0f / m1);

    return physicalValueToFrameBufferValue(l * pqC);
}

float inverseEotfSt2084(float v, float exponentScaleFactor = 1.0f)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f * exponentScaleFactor;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    const float pqC = 10000.0f;

    float physical = frameBufferValueToPhysicalValue(v);
    float y = physical / pqC;

    float ym = pow(y, m1);
    return exp2(m2 * (log2(c1 + c2 * ym) - log2(1.0f + c3 * ym)));
}

// -----------------------------------------------------------------------------
// ICtCp Color Space Conversion
// -----------------------------------------------------------------------------
float3 rgbToICtCp(float3 rgb)
{
    float l = (rgb.r * 1688.0f + rgb.g * 2146.0f + rgb.b * 262.0f) / 4096.0f;
    float m = (rgb.r * 683.0f + rgb.g * 2951.0f + rgb.b * 462.0f) / 4096.0f;
    float s = (rgb.r * 99.0f + rgb.g * 309.0f + rgb.b * 3688.0f) / 4096.0f;

    float lPQ = inverseEotfSt2084(l);
    float mPQ = inverseEotfSt2084(m);
    float sPQ = inverseEotfSt2084(s);

    float3 ictCp;
    ictCp.x = (2048.0f * lPQ + 2048.0f * mPQ) / 4096.0f;
    ictCp.y = (6610.0f * lPQ - 13613.0f * mPQ + 7003.0f * sPQ) / 4096.0f;
    ictCp.z = (17933.0f * lPQ - 17390.0f * mPQ - 543.0f * sPQ) / 4096.0f;
    
    return ictCp;
}

float3 iCtCpToRgb(float3 ictCp)
{
    float l = ictCp.x + 0.00860904f * ictCp.y + 0.11103f * ictCp.z;
    float m = ictCp.x - 0.00860904f * ictCp.y - 0.11103f * ictCp.z;
    float s = ictCp.x + 0.560031f * ictCp.y - 0.320627f * ictCp.z;

    float lLin = eotfSt2084(l);
    float mLin = eotfSt2084(m);
    float sLin = eotfSt2084(s);

    float3 rgb;
    rgb.r = max(3.43661f * lLin - 2.50645f * mLin + 0.0698454f * sLin, 0.0f);
    rgb.g = max(-0.79133f * lLin + 1.9836f * mLin - 0.192271f * sLin, 0.0f);
    rgb.b = max(-0.0259499f * lLin - 0.0989137f * mLin + 1.12486f * sLin, 0.0f);
    
    return rgb;
}

// -----------------------------------------------------------------------------
// Jzazbz Color Space Conversion
// -----------------------------------------------------------------------------
float3 rgbToJzazbz(float3 rgb)
{
    float l = rgb.r * 0.530004f + rgb.g * 0.355704f + rgb.b * 0.086090f;
    float m = rgb.r * 0.289388f + rgb.g * 0.525395f + rgb.b * 0.157481f;
    float s = rgb.r * 0.091098f + rgb.g * 0.147588f + rgb.b * 0.734234f;

    float lPQ = inverseEotfSt2084(l, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float mPQ = inverseEotfSt2084(m, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float sPQ = inverseEotfSt2084(s, JZAZBZ_EXPONENT_SCALE_FACTOR);

    float iz = 0.5f * lPQ + 0.5f * mPQ;

    float3 jab;
    jab.x = (0.44f * iz) / (1.0f - 0.56f * iz) - 1.6295499532821566e-11f;
    jab.y = 3.524000f * lPQ - 4.066708f * mPQ + 0.542708f * sPQ;
    jab.z = 0.199076f * lPQ + 1.096799f * mPQ - 1.295875f * sPQ;
    
    return jab;
}

float3 jzazbzToRgb(float3 jab)
{
    float jz = jab.x + 1.6295499532821566e-11f;
    float iz = jz / (0.44f + 0.56f * jz);
    float a = jab.y;
    float b = jab.z;

    float l = iz + a * 1.386050432715393e-1f + b * 5.804731615611869e-2f;
    float m = iz + a * -1.386050432715393e-1f + b * -5.804731615611869e-2f;
    float s = iz + a * -9.601924202631895e-2f + b * -8.118918960560390e-1f;

    float lLin = eotfSt2084(l, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float mLin = eotfSt2084(m, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float sLin = eotfSt2084(s, JZAZBZ_EXPONENT_SCALE_FACTOR);

    float3 rgb;
    rgb.r = lLin * 2.990669f + mLin * -2.049742f + sLin * 0.088977f;
    rgb.g = lLin * -1.634525f + mLin * 3.145627f + sLin * -0.483037f;
    rgb.b = lLin * -0.042505f + mLin * -0.377983f + sLin * 1.448019f;
    
    return rgb;
}

// -----------------------------------------------------------------------------
// Unified Color Space Functions
// -----------------------------------------------------------------------------
float3 rgbToUcs(float3 rgb)
{
#if TONE_MAPPING_UCS == TONE_MAPPING_UCS_ICTCP
    return rgbToICtCp(rgb);
#elif TONE_MAPPING_UCS == TONE_MAPPING_UCS_JZAZBZ
    return rgbToJzazbz(rgb);
#endif
}

float3 ucsToRgb(float3 ucs)
{
#if TONE_MAPPING_UCS == TONE_MAPPING_UCS_ICTCP
    return iCtCpToRgb(ucs);
#elif TONE_MAPPING_UCS == TONE_MAPPING_UCS_JZAZBZ
    return jzazbzToRgb(ucs);
#endif
}

// -----------------------------------------------------------------------------
// GT7 Tone Mapping Structure
// -----------------------------------------------------------------------------
struct GT7ToneMapping
{
    float sdrCorrectionFactor;
    float framebufferLuminanceTarget;
    float framebufferLuminanceTargetUcs;
    GTToneMappingCurveV2 curve;
    float blendRatio;
    float fadeStart;
    float fadeEnd;
};

GT7ToneMapping initializeHDR(float physicalTargetLuminance)
{
    GT7ToneMapping toneMapper;
    toneMapper.sdrCorrectionFactor = 1.0f;
    toneMapper.framebufferLuminanceTarget = physicalValueToFrameBufferValue(physicalTargetLuminance);
    
    // Initialize curve with GT7 parameters
    toneMapper.curve = initializeCurve(toneMapper.framebufferLuminanceTarget, 0.25f, 0.538f, 0.444f, 1.280f);
    
    // Default blend parameters
    toneMapper.blendRatio = 0.6f;
    toneMapper.fadeStart = 0.98f;
    toneMapper.fadeEnd = 1.16f;
    
    // Calculate UCS target luminance
    float3 rgb = float3(toneMapper.framebufferLuminanceTarget, 
                       toneMapper.framebufferLuminanceTarget, 
                       toneMapper.framebufferLuminanceTarget);
    float3 ucs = rgbToUcs(rgb);
    toneMapper.framebufferLuminanceTargetUcs = ucs.x;
    
    return toneMapper;
}

GT7ToneMapping initializeSDR()
{
    GT7ToneMapping toneMapper;
    toneMapper.sdrCorrectionFactor = 1.0f / physicalValueToFrameBufferValue(GRAN_TURISMO_SDR_PAPER_WHITE);
    toneMapper.framebufferLuminanceTarget = physicalValueToFrameBufferValue(GRAN_TURISMO_SDR_PAPER_WHITE);
    
    // Initialize curve with GT7 parameters
    toneMapper.curve = initializeCurve(toneMapper.framebufferLuminanceTarget, 0.25f, 0.538f, 0.444f, 1.280f);
    
    // Default blend parameters
    toneMapper.blendRatio = 0.6f;
    toneMapper.fadeStart = 0.98f;
    toneMapper.fadeEnd = 1.16f;
    
    // Calculate UCS target luminance
    float3 rgb = float3(toneMapper.framebufferLuminanceTarget, 
                       toneMapper.framebufferLuminanceTarget, 
                       toneMapper.framebufferLuminanceTarget);
    float3 ucs = rgbToUcs(rgb);
    toneMapper.framebufferLuminanceTargetUcs = ucs.x;
    
    return toneMapper;
}

// -----------------------------------------------------------------------------
// Main Tone Mapping Function
// -----------------------------------------------------------------------------
float3 applyGT7ToneMapping(GT7ToneMapping toneMapper, float3 rgb)
{
    // Convert to UCS to separate luminance and chroma
    float3 ucs = rgbToUcs(rgb);

    // Per-channel tone mapping ("skewed" color)
    float3 skewedRgb = float3(
        evaluateCurve(toneMapper.curve, rgb.r),
        evaluateCurve(toneMapper.curve, rgb.g),
        evaluateCurve(toneMapper.curve, rgb.b)
    );

    float3 skewedUcs = rgbToUcs(skewedRgb);

    float chromaScale = chromaCurve(ucs.x / toneMapper.framebufferLuminanceTargetUcs, 
                                   toneMapper.fadeStart, toneMapper.fadeEnd);

    float3 scaledUcs = float3(
        skewedUcs.x,
        ucs.y * chromaScale,
        ucs.z * chromaScale
    );

    // Convert back to RGB
    float3 scaledRgb = ucsToRgb(scaledUcs);

    // Final blend between per-channel and UCS-scaled results
    float3 blended = lerp(skewedRgb, scaledRgb, toneMapper.blendRatio);
    
    // Apply SDR correction factor and clamp
    float3 result = toneMapper.sdrCorrectionFactor * min(blended, toneMapper.framebufferLuminanceTarget);
    
    return result;
}

// -----------------------------------------------------------------------------
// Convenience Functions
// -----------------------------------------------------------------------------

// Apply GT7 tone mapping for HDR output
float3 GT7ToneMappingHDR(float3 linearRgb, float targetLuminanceNits)
{
    linearRgb *= Params[0].r;
    GT7ToneMapping toneMapper = initializeHDR(targetLuminanceNits);
    return applyGT7ToneMapping(toneMapper, linearRgb);
}

// Apply GT7 tone mapping for SDR output
float3 GT7ToneMappingSDR(float3 linearRgb)
{
    linearRgb *= Params[0].r;
    GT7ToneMapping toneMapper = initializeSDR();
    return applyGT7ToneMapping(toneMapper, linearRgb);
}

#endif // GT7_TONE_MAPPING_HLSLI