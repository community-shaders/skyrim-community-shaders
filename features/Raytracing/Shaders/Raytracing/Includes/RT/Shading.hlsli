#ifndef SHADING_HLSL
#define SHADING_HLSL

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/AdvancedSettings.hlsli"

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/MonteCarlo.hlsli"
#include "Raytracing/Includes/Surface.hlsli"

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

float3 Diffuse(float roughness, float3 N, float3 V, float3 L, float NdotV, float NdotL, float VdotH, float VdotL, float NdotH)
{
#if DIFFUSE_MODE == DIFFUSE_MODE_BURLEY
    return BRDF::Diffuse_Burley(roughness, NdotV, NdotL, VdotH);
#elif DIFFUSE_MODE == DIFFUSE_MODE_ORENNAYAR
    return BRDF::Diffuse_OrenNayar(roughness, N, V, L, NdotV, NdotL);
#elif DIFFUSE_MODE == DIFFUSE_MODE_GOTANDA
    return BRDF::Diffuse_Gotanda(roughness, NdotV, NdotL, VdotL);
#elif DIFFUSE_MODE == DIFFUSE_MODE_CHAN
    return BRDF::Diffuse_Chan(roughness, NdotV, NdotL, VdotH, NdotH);    
#else
    return BRDF::Diffuse_Lambert();
#endif
}

float3 EvalDiffuse(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NoL = saturate(dot(surface.Normal, l));
     
    if (NoL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    // Diffuse is meant to be very light (and used with DDGI), so I don't see much point in using a different diffuse or shading model here
    return surface.DiffuseAlbedo * NoL * BRDF::Diffuse_Lambert() * Frame.Diffuse;
}

float3 EvalDefaultBRDF(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NoL = saturate(dot(surface.Normal, l));
     
    if (NoL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 H = normalize(brdfContext.ViewDirection + l);
        
    float NoH = saturate(dot(surface.Normal, H));
    float VoH = clamp(dot(brdfContext.ViewDirection, H), 1e-5f, 1.0f);
    float VoL = saturate(dot(brdfContext.ViewDirection, l));
    
    float D = BRDF::D_GGX(surface.Roughness, NoH);
    float Vis = BRDF::Vis_SmithJointApprox(surface.Roughness, max(1e-5f, brdfContext.NdotV), NoL);
    float3 F = BRDF::F_Schlick(surface.F0, VoH);
    
    // specular BRDF
    float3 Fr = (D * Vis) * F * Frame.Specular;
    
    float3 Fd = surface.DiffuseAlbedo * Frame.Diffuse 
    * Diffuse(surface.Roughness, surface.Normal, brdfContext.ViewDirection, l, brdfContext.NdotV, NoL, VoH, VoL, NoH) 
    * ShadowTerminatorTerm(l, surface.Normal, surface.GeomNormal);
    
    return (Fd + Fr) * NoL;
}

float3 EvalLight(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
#if LIGHTEVAL_MODE == LIGHTEVAL_MODE_DIFFUSE
    return EvalDiffuse(l, surface, brdfContext); 
#else
    return EvalDefaultBRDF(l, surface, brdfContext);
#endif
}

float3 EvalDirectionalLight(in Surface surface, in BRDFContext brdfContext, in Light light, inout uint randomSeed)
{
    float3 direct = EvalLight(light.Vector, surface, brdfContext) * light.Color;

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(light.Vector, SampleCosineHemisphereScaled(randomSeed, 0.025f));  
        direct *= TraceRayShadow(Scene, surface.Position, lr);
    }

    return direct;
}

float3 EvalPointLight(in Surface surface, in BRDFContext brdfContext, in LightData lightData, inout uint randomSeed)
{
    if (lightData.Count == 0) 
        return float3(0, 0, 0);
    
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 l = (light.Vector - surface.Position);
    float dist = length(l);      
    l /= dist;
    
    // float atten = VanillaSquaredAtten(dist, light.Range);
    float atten = InverseSquareAtten(dist * GAME_UNIT_TO_M, light.Range * 64); // This is temporal
    
    float3 direct = EvalLight(l, surface, brdfContext) * atten * light.Color * float(lightData.Count);

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.05f));        
        direct *= TraceRayShadowFinite(Scene, surface.Position, lr, dist);
    }
    
    return direct;
}

void SampleDefaultBRDF(in Surface surface, in BRDFContext brdfContext, inout uint randomSeed, out float3 direction, out float3 brdfWeight)
{
    const float3 V = brdfContext.ViewDirection;
    float3 L = 0;
    float NdotL = 0;

    brdfWeight = 0.0f;

    const float specularProb = lerp(MonteCarlo::GetSpecularBrdfProbability(surface, V, surface.Normal), 1.0f, surface.Metallic);
    const bool isSpecular = Random(randomSeed) < specularProb;

    float3 brdf = 0.0f;
    float pdf = 0.0f;
    float diffusePdf = 0.0f;
    float specularPdf = 0.0f;

    float3 Ve = float3(
            dot(brdfContext.ViewDirection, surface.Tangent), 
            dot(brdfContext.ViewDirection, surface.Bitangent), 
            dot(brdfContext.ViewDirection, surface.Normal)
        );
    float3 Le = 0.0f;
    float3 He = 0.0f;

    const float alpha = surface.Roughness * surface.Roughness;
    const float alpha2 = alpha * alpha;

    [branch]
    if (!isSpecular)
    {
        Le = SampleCosineHemisphere(randomSeed);
        He = normalize(Ve + Le);
    }
    else
    {
        // float2 E = Get2D(randomSeed);
        // E.x = MonteCarlo::RescaleRandomNumber(E.x, specularProb, 1.0f);
        // float2 Xi = MonteCarlo::Hammersley(0, 1, random2D);
        He = MonteCarlo::SampleGGX_VNDF(Ve, alpha, randomSeed);
        // He = MonteCarlo::ImportanceSampleVisibleGGX(E, alpha, Ve).xyz;
        Le = reflect(-Ve, He);
    }

    L = surface.Mul(Le);
    float3 H = surface.Mul(He);
    NdotL = saturate(dot(surface.Normal, L));
    float VdotH = saturate(dot(He, Ve));
    float NdotH = saturate(dot(surface.Normal, H));
    float VdotL = saturate(dot(Ve, Le));
    
    // const float2 GGXResult = MonteCarlo::GGXEvalReflection(Le, Ve, He, alpha);
    specularPdf = MonteCarlo::SampleGGXVNDFReflectionPdf(alpha, alpha2, NdotH, brdfContext.NdotV, VdotH);
    diffusePdf = NdotL / Math::PI;

    float3 F = BRDF::F_Schlick(surface.F0, VdotH);

    float3 Fd = Frame.Diffuse * surface.DiffuseAlbedo * NdotL 
        * Diffuse(surface.Roughness, surface.Normal, V, L, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH) 
        * ShadowTerminatorTerm(L, surface.Normal, surface.GeomNormal);
    
    float3 Fr = Frame.Specular * F * MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, brdfContext.NdotV, VdotH, NdotH) * specularPdf;
#if GGX_ENERGY_CONSERVATION
    Fr *= BRDF::GGXEnergyConservationTerm(surface.F0, surface.Roughness, brdfContext.NdotV);
#endif

    pdf = (1.0f - specularProb) * diffusePdf + specularProb * specularPdf;

    brdf = Fd + Fr;

    brdfWeight = brdf / max(pdf, 1e-7f);

    direction = L;
}

// Samples the sky hemisphere texture based on the given direction
// Output is in true linear space
float3 SampleSky(float3 dir)
{
    dir.z = max(dir.z, 0.0f);
    
    float r = sqrt(1.0f - dir.z);
    float phi = atan2(dir.y, dir.x);
    
    float2 disk = float2(r * cos(phi), r * sin(phi));
    float2 uv = disk * 0.5f + 0.5f;

    return Color::GammaToTrueLinear(SkyHemisphere.SampleLevel(BaseSampler, uv, 0.0f).rgb);
}

// Samples the direct radiance at the given surface point
float3 EvaluateRadiance(in Surface surface, in BRDFContext brdfContext, in Instance instance, in Material material, inout uint randomSeed)
{
    float3 radiance = surface.Emissive * Frame.Emissive;
        
    radiance += EvalDirectionalLight(surface, brdfContext, Frame.Directional, randomSeed);
    radiance += EvalPointLight(surface, brdfContext, instance.LightData, randomSeed);        

    /*if (material.PBRFlags & PBR::Flags::Subsurface)
    {
        // Do something expensive
    }*/    
    
    return radiance;
}

float3 Composite(bool isDiffusePath, float3 radiance, Surface surface, BRDFContext brdfContext)
{
    [branch]
    if (isDiffusePath)
    {
        float3 diffuseAO = MonteCarlo::DiffuseAO(surface.Albedo, surface.AO);
        float3 diffuse = radiance.rgb * diffuseAO * Frame.Diffuse;       
        return diffuse * surface.Albedo;      
    }
    else
    {
        float3 specularAO = MonteCarlo::SpecularAO(brdfContext.NdotV, surface.Roughness, surface.AO, surface.F0);
        float3 specular = radiance.rgb * specularAO * Frame.Specular;       
        return specular;          
    }
}

#endif // SHADING_HLSL