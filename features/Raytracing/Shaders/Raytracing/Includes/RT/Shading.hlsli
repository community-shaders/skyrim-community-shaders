#ifndef SHADING_HLSL
#define SHADING_HLSL

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/AdvancedSettings.hlsli"

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/ColorConversions.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/MonteCarlo.hlsli"
#include "Raytracing/Includes/Surface.hlsli"

#include "Raytracing/Includes/Materials/BSDF.hlsli"

static const float ISL_SCALE = 0.8f;
static const float ISL_METRES_TO_UNITS = 70.f;
static const float ISL_METRES_TO_UNITS_SQ = ISL_METRES_TO_UNITS * ISL_METRES_TO_UNITS;
static const float ISL_SCALED_UNITS_SQ = ISL_SCALE * ISL_METRES_TO_UNITS_SQ;

float InverseSquareAtten(float dist, float range)
{
    // Normalized inverse-square
    float atten = 1.0 / (1.0 + dist * dist);

    // Smooth fade to zero at range
    float fade = saturate(1.0 - dist / range);
    fade = fade * fade * (3.0 - 2.0 * fade);

    return atten * fade;
}

float LinearAtten(float dist, float range)
{
    return saturate(1.0 - dist / range);
}

float VanillaSquaredAtten(float dist, float range)
{
    return saturate(1.0 - dist * dist / (range * range));
}

float3 EvalDiffuse(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NdotL = saturate(dot(surface.Normal, l));

    if (NdotL <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    // Diffuse is meant to be very light (and used with DDGI), so I don't see much point in using a different diffuse or shading model here
    return surface.DiffuseAlbedo * NdotL * BRDF::Diffuse_Lambert();
}

float3 EvalLight(in float3 l, in Material material, in Surface surface, in BRDFContext brdfContext, in StandardBSDF bsdf)
{
#if LIGHTEVAL_MODE == LIGHTEVAL_MODE_DIFFUSE
    return EvalDiffuse(l, surface, brdfContext);
#else
    float4 bsdfEval = bsdf.Eval(brdfContext, material, surface, l);
    return bsdfEval.xyz;
#endif
}

float3 EvalDirectionalLight(in Material material, in Surface surface, in BRDFContext brdfContext, in DirectionalLight light, in StandardBSDF bsdf, inout uint randomSeed)
{
    light.Color = DirLightToLinear(light.Color);
    // Sun angular radius is ~0.00465 radians (~0.266 degrees)
    float cosSunDisk = cos(0.00465f);
    float3 lr = TangentToWorld(light.Vector, SampleConeUniform(randomSeed, cosSunDisk));
    float3 direct = EvalLight(lr, material, surface, brdfContext, bsdf) * light.Color;

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        direct *= TraceRayShadow(Scene, surface, lr, randomSeed);
    }

    return direct;
}

float GetLightSampleWeight(Surface surface, Light light)
{
    float3 l = (light.Vector - surface.Position);
    float dist = length(l) * GAME_UNIT_TO_M;
    float atten = 1.0 / (1.0 + dist * dist);
    float intensity = max(light.Color.r, max(light.Color.g, light.Color.b)) * light.Fade;
    return atten * intensity;
}

float3 EvalPointLight(in Material material, in Surface surface, in BRDFContext brdfContext, in LightData lightData, in StandardBSDF bsdf, inout uint randomSeed)
{
    if (lightData.Count == 0)
        return float3(0, 0, 0);

    float lightWeight = float(lightData.Count);

#if defined(RIS)
    const uint candidateCount = min(RIS_MAX_CANDIDATES, lightData.Count);
    uint selectedLightID = 0;
    float totalWeight = 0.0f;
    float selectedWeight = 0.0f;

    for (uint i = 0; i < candidateCount; i++)
    {
        uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);
        uint lightID = lightData.GetID(lightIdx);
        Light testLight = Lights[lightID];
        const bool isTestLinear = (testLight.Flags & LightFlags::LinearLight) != 0;
        testLight.Color = PointLightToLinear(testLight.Color, isTestLinear);
        float weight = GetLightSampleWeight(surface, testLight);
        totalWeight += weight;

        if (Random(randomSeed) * totalWeight < weight)
        {
            selectedLightID = lightID;
            selectedWeight = weight;
        }
    }
    if (totalWeight == 0.0f)
        return float3(0, 0, 0);

    float risWeight = (totalWeight / max(selectedWeight, 1e-7f)) / float(candidateCount);

    lightWeight *= risWeight;

    Light light = Lights[selectedLightID];
#else

    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);

    Light light = Lights[lightID];
#endif

    const bool isLinear = (light.Flags & LightFlags::LinearLight) != 0;
    light.Color = PointLightToLinear(light.Color, isLinear);

    float3 l = (light.Vector - surface.Position);
    float dist = length(l);
    l /= dist;

    float lightSourceAngle = 0.05f;

	float atten = 0.0f;
	if ((light.Flags & LightFlags::ISL) != 0)
	{
		float invSq = ISL_SCALED_UNITS_SQ * rcp(dist * dist + light.SizeBias);
		float t = saturate((light.Radius - dist) * light.FadeZone);
		float fastSmoothstep = t * t * (3.0f - 2.0f * t);
		atten = invSq * fastSmoothstep;
        float size = sqrt((light.SizeBias * 2.0f) / (0.8 * 4900));
        lightSourceAngle = atan2(size, dist);
	}
	else
	{
		float intensityFactor = saturate(dist * light.InvRadius);
		atten = 1.0f - intensityFactor * intensityFactor;
	}

    float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, lightSourceAngle));

    float3 direct = EvalLight(lr, material, surface, brdfContext, bsdf) * atten * light.Color * light.Fade * lightWeight;

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        direct *= TraceRayShadowFinite(Scene, surface, lr, dist, randomSeed);
    }

    return direct;
}

float2 EvalHemiUV(float3 dir)
{
    dir.z = max(dir.z, 0.0f);

    float r = sqrt(1.0f - dir.z);
    float phi = atan2(dir.y, dir.x);

    float2 disk = float2(cos(phi), sin(phi)) * r;
    return disk * 0.5f + 0.5f;
}

// Samples the sky hemisphere texture based on the given direction
// Output is in true linear space
float3 SampleSky(float3 dir)
{
    float2 uv = EvalHemiUV(dir);

    float3 color = SkyHemisphere.SampleLevel(BaseSampler, uv, 0.0f).rgb;

    return LLGammaToTrueLinear(color);
}

float EvalSkyOcclusion(float3 dir)
{
    float2 uv = EvalHemiUV(dir);

    return lerp(1.0f, 1.0f - SkyHemisphere.SampleLevel(BaseSampler, uv, 0.0f).a, Frame.CloudOpacity);
}

float3 EvaluateDirectRadiance(in Material material, in Surface surface, in BRDFContext brdfContext, in Instance instance, in StandardBSDF bsdf, inout uint randomSeed)
{
    float3 radiance = EvalDirectionalLight(material, surface, brdfContext, Frame.Directional, bsdf, randomSeed) * EvalSkyOcclusion(Frame.Directional.Vector);
    radiance += EvalPointLight(material, surface, brdfContext, instance.LightData, bsdf, randomSeed);

    return radiance;
}

bool ComputeTangentSpace(inout Surface surface, const bool ignoreTangent)
{
    // Check that tangent space exists and can be safely orthonormalized.
    // Otherwise invent a tanget frame based on the normal.
    // We check that:
    //  - Tangent exists, this is indicated by a nonzero sign (w).
    //  - It has nonzero length. Zeros can occur due to interpolation or bad assets.
    //  - It is not parallel to the normal. This can occur due to normal mapping or bad assets.
    //  - It does not have NaNs. These will propagate and trigger the fallback.

    float NdotT = dot(surface.GeomTangent, surface.Normal);
    bool nonParallel = abs(NdotT) < 0.9999f;
    bool nonZero = dot(surface.GeomTangent, surface.GeomTangent) > 0.f;

    bool valid = nonZero && nonParallel;
    if (!ignoreTangent && valid)
    {
        surface.Tangent = normalize(surface.GeomTangent - surface.Normal * NdotT);
        surface.Bitangent = cross(surface.Normal, surface.Tangent);
    }
    else
    {
        surface.Tangent = perp_stark(surface.Normal);
        surface.Bitangent = cross(surface.Normal, surface.Tangent);
    }

    return valid;
}

void AdjustShadingNormal(inout Surface surface, BRDFContext brdfContext, uniform bool recomputeTangentSpace, const bool ignoreTangent)
{
    float3 Ng = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.f ? surface.FaceNormal : -surface.FaceNormal;
    float signN = dot(surface.Normal, Ng) >= 0.f ? 1.f : -1.f;
    float3 Ns = signN * surface.Normal;

    // Blend the shading normal towards the geometric normal at grazing angles.
    // This is to avoid the view vector from becoming back-facing.
    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(brdfContext.ViewDirection, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        surface.Normal = signN * normalize(lerp(Ng, Ns, t));
    }
    if (cosTheta <= kCosThetaThreshold || recomputeTangentSpace)
        ComputeTangentSpace(surface, ignoreTangent);
}

#endif // SHADING_HLSL