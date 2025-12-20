#ifndef SHADING_HLSL
#define SHADING_HLSL

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/BRDF.hlsli"
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

float3 LambertianDirectD(in Surface surface, in Light light, inout uint randomSeed)
{
    float3 l = normalize(light.Vector);
 
    float NdotL = saturate(dot(surface.Normal, l));
            
    float3 direct = NdotL * light.Color * surface.Albedo;
    
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.025f));  
        direct *= TraceRayShadow(Scene, surface.Position, lr);
    }      
    
    return direct; 
}

float3 LambertianDirectP(in Surface surface, in LightData lightData, inout uint randomSeed)
{
    if (lightData.Count == 0)
        return 0;
    
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 l = (light.Vector - surface.Position);
    float dist = length(l);      
    l /= dist;
    
    float atten = LinearAtten(dist, light.Range);
    //float atten = InverseSquareAtten(dist, light.Range); // This requires all lights to be ISL enabled
            
    float NdotL = saturate(dot(surface.Normal, l)) * atten;

    float3 direct  = NdotL * light.Color * surface.Albedo * float(lightData.Count);
 
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.05f));        
        direct *= TraceRayShadowFinite(Scene, surface.Position, lr, dist);
    }    
    
    return direct; // (albedo / Math::PI)
}

float3 GGXDirect(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NoL = saturate(dot(surface.Normal, l));
     
    if (NoL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 h = normalize(brdfContext.ViewDirection + l);
        
    float NoH = saturate(dot(surface.Normal, h));
    float VoH = clamp(dot(brdfContext.ViewDirection, h), 1e-5f, 1.0f);

    float D = BRDF::D_GGXAlpha(NoH, surface.Roughness);
    float V = BRDF::V_SmithGGXCorrelatedFast(max(1e-5f, brdfContext.NdotV), NoL, surface.Roughness);
    float3 F = BRDF::F_Schlick(surface.F0, VoH);
    
    // specular BRDF
    float3 Fr = (D * V) * F;
    float3 Fd = surface.DiffuseAlbedo; // / Math::PI;
    
    return (Fd + Fr) * NoL;
}

float3 GGXDirectD(in Surface surface, in BRDFContext brdfContext, in Light light, inout uint randomSeed)
{
    float3 direct = GGXDirect(light.Vector, surface, brdfContext) * light.Color;

    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(light.Vector, SampleCosineHemisphereScaled(randomSeed, 0.025f));  
        direct *= TraceRayShadow(Scene, surface.Position, lr);
    }

    return direct;
}

float3 GGXDirectP(in Surface surface, in BRDFContext brdfContext, in LightData lightData, inout uint randomSeed)
{
    if (lightData.Count == 0) 
        return float3(0, 0, 0);
    
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 l = (light.Vector - surface.Position);
    float dist = length(l);      
    l /= dist;
    
    float atten = LinearAtten(dist, light.Range);
    //float atten = InverseSquareAtten(dist, light.Range); // This requires all lights to be ISL enabled
    
    float3 direct = GGXDirect(l, surface, brdfContext) * atten * light.Color * float(lightData.Count);

    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.05f));        
        direct *= TraceRayShadowFinite(Scene, surface.Position, lr, dist);
    }
    
    return direct;
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
float3 SampleRadiance(in Surface surface, in BRDFContext brdfContext, in Instance instance, in Material material, inout uint randomSeed)
{
    float3 radiance = surface.Emissive * Frame.Emissive;
    
#if defined(LAMBERT)
    radiance += LambertianDirectD(surface, Frame.Directional, randomSeed);
    radiance += LambertianDirectP(surface, instance.LightData, randomSeed);
#else
    radiance += GGXDirectD(surface, brdfContext, Frame.Directional, randomSeed);
    radiance += GGXDirectP(surface, brdfContext, instance.LightData, randomSeed);
#endif    
    
    return radiance;
}

float3 Composite(bool isDiffusePath, float3 radiance, Surface surface, BRDFContext brdfContext)
{
    [branch]
    if (isDiffusePath)
    {
        float3 diffuseAO = BRDF::DiffuseAO(surface.Albedo, surface.AO);
        float3 diffuse = radiance.rgb * diffuseAO * Frame.Diffuse;       
        return diffuse * surface.Albedo;      
    }
    else
    {
        float3 specularAO = BRDF::SpecularAO(brdfContext.NdotV, surface.Roughness, surface.AO, surface.F0);
        float3 specular = radiance.rgb * specularAO * Frame.Specular;       
        return specular;          
    }
}

#endif // SHADING_HLSL