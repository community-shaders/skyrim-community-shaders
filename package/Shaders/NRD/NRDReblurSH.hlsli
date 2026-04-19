// Self-contained NRD REBLUR SH helpers — inlined from NRD v4 (NRD.hlsli).
// Config: NRD_NORMAL_ENCODING=2 (R10G10B10A2_UNORM), NRD_ROUGHNESS_ENCODING=1 (LINEAR).
// Do not modify without re-checking against extern/NRD/Shaders/NRD.hlsli.

#ifndef NRD_REBLUR_SH_HLSLI
#define NRD_REBLUR_SH_HLSLI

#define NRD_FP16_MAX    65504.0
#define NRD_PI          3.14159265358979323846
#define NRD_EPS         1e-6
#define NRD_INF         1e6

// --- Private helpers ---

float3 _NRD_LinearToYCoCg(float3 color)
{
    float Y  = dot(color, float3(0.25,  0.5,  0.25));
    float Co = dot(color, float3(0.5,   0.0,  -0.5));
    float Cg = dot(color, float3(-0.25, 0.5,  -0.25));
    return float3(Y, Co, Cg);
}

float3 _NRD_YCoCgToLinear(float3 color)
{
    float t  = color.x - color.z;
    float3 r;
    r.y = color.x + color.z;
    r.x = t + color.y;
    r.z = t - color.y;
    return max(r, 0.0);
}

float3 _NRD_YCoCgToLinear_Corrected(float Y, float Y0, float2 CoCg)
{
    Y = max(Y, 0.0);
    CoCg *= (Y + NRD_EPS) / (Y0 + NRD_EPS);
    return _NRD_YCoCgToLinear(float3(Y, CoCg));
}

bool _NRD_IsInvalidF(float x)  { return isnan(x) || isinf(x); }
bool _NRD_IsInvalidF3(float3 x) { return any(isnan(x)) || any(isinf(x)); }

float3 _NRD_SafeNormalize(float3 v) { return v * rsqrt(dot(v, v) + 1e-9); }

float _NRD_Pow5(float x) { return pow(saturate(1.0 - x), 5.0); }

float _NRD_DiffuseTerm(float roughness, float NoL, float NoV, float VoH)
{
    float f   = 2.0 * VoH * VoH * roughness - 0.5;
    float FdV = f * _NRD_Pow5(NoV) + 1.0;
    float FdL = f * _NRD_Pow5(NoL) + 1.0;
    return FdV * FdL / NRD_PI;
}

// --- NRD_SG struct ---

struct NRD_SG
{
    float  c0;
    float2 chroma;
    float  normHitDist;
    float3 c1;
    float  sharpness;
};

NRD_SG _NRD_SG_Create(float3 radiance, float3 direction, float normHitDist)
{
    float3 YCoCg = _NRD_LinearToYCoCg(radiance);
    NRD_SG sg;
    sg.c0          = YCoCg.x;
    sg.chroma      = YCoCg.yz;
    sg.c1          = direction * YCoCg.x;
    sg.normHitDist = normHitDist;
    sg.sharpness   = 0.0;
    return sg;
}

float3 _NRD_SG_ExtractDirection(NRD_SG sg)
{
    return sg.c1 / max(length(sg.c1), NRD_EPS);
}

float _NRD_SG_IntegralApprox(NRD_SG sg)
{
    return 2.0 * NRD_PI * (sg.c0 / sg.sharpness);
}

float _NRD_SG_InnerProduct(NRD_SG a, NRD_SG b)
{
    float3 dir = a.sharpness * a.c1 + b.sharpness * b.c1;
    float  d   = length(dir);
    float  c   = exp(d - a.sharpness - b.sharpness);
    c *= 1.0 - exp(-2.0 * d);
    c /= max(d, NRD_EPS);
    return 2.0 * NRD_PI * c * a.c0 * b.c0;
}

// --- Normal packing (NRD_NORMAL_ENCODING = 2, R10G10B10A2_UNORM) ---
// Improved oct-packing with roughness in the z-channel sign.

float3 _NRD_EncodeNormalRoughness101010(float3 n, float roughness)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float3 r;
    r.y = n.y * 0.5 + 0.5;
    r.x = n.x * 0.5 + r.y;
    r.y -= n.x * 0.5;
    roughness = max(roughness, 1.5 / 512.0);
    float s = n.z < 0 ? -roughness : roughness;
    r.z = s * 0.5 + 0.5;
    return r;
}

// Pack world-space normal + linear roughness + materialID into R10G10B10A2.
float4 NRD_FrontEnd_PackNormalAndRoughness(float3 N, float roughness, float materialID)
{
    // NRD_ROUGHNESS_ENCODING = 1 (LINEAR): no transform needed
    float4 p;
    p.xyz = _NRD_EncodeNormalRoughness101010(N, roughness);
    p.w   = saturate(materialID / 3.0);
    return p;
}

// --- REBLUR front-end SH packing ---

float4 REBLUR_FrontEnd_PackSh(float3 radiance, float normHitDist, float3 direction, out float4 out1, bool sanitize)
{
    if (sanitize) {
        radiance    = _NRD_IsInvalidF3(radiance)    ? float3(0, 0, 0) : clamp(radiance, 0, NRD_FP16_MAX);
        normHitDist = _NRD_IsInvalidF(normHitDist)  ? 0 : saturate(normHitDist);
        direction   = _NRD_IsInvalidF3(direction)   ? float3(0, 0, 0) : clamp(direction, -1.0, 1.0);
    }
    NRD_SG sg = _NRD_SG_Create(radiance, direction, normHitDist);
    float4 out0 = float4(sg.c0, sg.chroma, sg.normHitDist);
    out1        = float4(sg.c1, sg.sharpness);
    return out0;
}

// --- REBLUR back-end SH unpacking ---

NRD_SG REBLUR_BackEnd_UnpackSh(float4 sh0, float4 sh1)
{
    NRD_SG sg;
    sg.c0          = sh0.x;
    sg.chroma      = sh0.yz;
    sg.normHitDist = sh0.w;
    sg.c1          = sh1.xyz;
    sg.sharpness   = sh1.w;
    return sg;
}

// --- NRD_SG resolve ---

float3 NRD_SG_ResolveDiffuse(NRD_SG sg, float3 N, float3 V, float roughness)
{
    float3 L   = _NRD_SG_ExtractDirection(sg);
    float  NoL = saturate(dot(N, L));

    NRD_SG light;
    light.sharpness = 2.0;
    light.c0        = sg.c0 * light.sharpness;
    light.c1        = L;

    NRD_SG ndf;
    ndf.c0        = 1.0;
    ndf.c1        = N;
    ndf.sharpness = 2.0;

    float Y = _NRD_SG_InnerProduct(ndf, light);

    float3 H   = normalize(L + V);
    float  NoV = abs(dot(N, V));
    float  VoH = abs(dot(V, H));
    float  Kdiff = _NRD_DiffuseTerm(roughness, NoL, NoV, VoH);
    Y *= Kdiff;
    Y *= lerp(1.0, lerp(1.5, 0.6, roughness), _NRD_Pow5(NoV));
    Y  = max(Y, sg.c0 / NRD_PI);

    return _NRD_YCoCgToLinear_Corrected(Y, sg.c0, sg.chroma);
}

#endif // NRD_REBLUR_SH_HLSLI
