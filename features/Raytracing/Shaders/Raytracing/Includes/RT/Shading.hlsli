#ifndef SHADING_HLSI
#define SHADING_HLSI

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"

#include "Raytracing/Includes/RT/microfacetBRDFUtils.hlsli"

float InverseSquareAtten2(float dist, float range)
{
    // Inverse square base
    float atten = 1.0 / (dist * dist + 0.0001);

    // Smooth fade to zero at range
    float fade = saturate(1.0 - dist / range);
    fade = fade * fade * (3.0 - 2.0 * fade);

    return atten * fade;
}

float InverseSquareAtten(float dist, float range)
{
    // Normalized inverse-square (scale-agnostic)
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

float3 LambertianDirectD(in float3 position, in float3 normal, in float3 albedo, in Light light)
{
    float3 L = normalize(light.Vector);
 
    float NdotL = saturate(dot(normal, L)) * TraceRayShadow(Scene, position, L);
            
    return NdotL * light.Color * albedo * Frame.Diffuse; 
}

float3 LambertianDirectP(in float3 position, in float3 normal, in float3 albedo, in LightData lightData, inout uint randomSeed)
{
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 lightVector = (light.Vector - position) * GAME_UNIT_TO_M;
    float lightDistanceSqr = dot(lightVector, lightVector);
    float lightDistance = sqrt(lightDistanceSqr);
        
    lightVector /= lightDistance;
         
    float attenuation = 1.0 / max(lightDistanceSqr, 0.01);
    float fade = saturate(1.0 - pow(lightDistance / light.Range, 4.0));
            
    float NdotL= saturate(dot(normal, lightVector)) * attenuation * fade * fade;
    NdotL *= float(lightData.Count) * TraceRayShadowFinite(Scene, position, lightVector, lightDistance * M_TO_GAME_UNIT);
            
    return NdotL * light.Color * albedo * Frame.Diffuse; // (albedo / Math::PI)
}

float3 LambertianIndirect(float3 position, float3 normal, float3 albedo, uint depth, inout uint randomSeed)
{
    float3 tangentSample = CosineSampleHemisphere(randomSeed);
    float3 randomDirection = TangentToWorld(normal, tangentSample);
            
    float3 bounceColor = TraceRayIndirect(Scene, position, randomDirection, depth, randomSeed).rgb;
    
    return bounceColor * albedo * Frame.Diffuse;
}

float3 GGXDirect(in float3 l, in float3 n, in float3 v, in float3 albedo, in float roughness, in float metalness)
{
    float NoL = saturate(dot(n, l));
     
    if (NoL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 h = normalize(v + l);
        
    float NoH = saturate(dot(n, h));
    float NoV = max(dot(n, v), 1e-5f);
    float VoH = saturate(dot(v, h));

    float D = D_GGX(NoH, roughness);
    float V = V_SmithGGXCorrelatedFast(NoV, NoL, roughness);
    float3 F = F_Schlick(VoH, F0(albedo, metalness));
    
    // specular BRDF
    float3 Fr = (D * V) * F;

    float3 diffuseColor = (1.0 - metalness) * albedo;
    float3 Fd = diffuseColor * (1.0 - F) * Fd_Lambert();

    return (Fd + Fr) * NoL;
}

float3 GGXDirectD(in float3 position, in float3 n, in float3 v, in float3 albedo, in float roughness, in float metalness, in Light light)
{
    float3 l = light.Vector;

    float3 diffuse = GGXDirect(l, n, v, albedo, roughness, metalness);

    float3 atten = TraceRayShadow(Scene, position, l);

    return diffuse * (light.Color * atten) * Frame.Diffuse;
}

float3 GGXDirectP(in float3 position, in float3 n, in float3 v, in float3 albedo, in float roughness, in float metalness, in LightData lightData, inout uint randomSeed)
{
    if (lightData.Count == 0) return float3(0, 0, 0);
    
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 l = (light.Vector - position);
    float dist = length(l);      
    l /= dist;
    
    float3 diffuse = GGXDirect(l, n, v, albedo, roughness, metalness);
    
    //float atten = InverseSquareAtten(dist, light.Range); // This requires all lights to be ISL enabled
    float atten = LinearAtten(dist, light.Range) * TraceRayShadowFinite(Scene, position, l, dist);    

    return diffuse * (light.Color * atten) * float(lightData.Count) * Frame.Diffuse;
}

float4 GGXIndirect(in float3 position, in float3 GN, float3x3 TBN, in float3 v, in float3 albedo, in float roughness, in float metalness, in float ao, in uint depth, inout uint randomSeed)
{  
    float3 n = TBN[2];
    
    float3 f0 = F0(albedo, metalness);
  
    float NoV = max(dot(n, v), 1e-5f);      
    
    float diffuseProbability = DiffuseProbability(roughness, metalness, f0, NoV);
    bool chooseDiffuse = (Random(randomSeed) < diffuseProbability);
    
    if (chooseDiffuse)
    {
        float3 l_tan = CosineSampleHemisphere(randomSeed);
        float3 l = normalize(mul(TBN, l_tan));

        float NoL = dot(n, l);
        
        if (NoL <= 0.0f)
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        NoL = saturate(NoL);
        
        float3 bounceColor = TraceRayIndirect(Scene, position + (GN * GN_OFFSET), l, depth, randomSeed).rgb * ao;

        float3 diffuseColor = (1.0f - metalness) * albedo;
        
        float3 finalDiffuse = bounceColor * diffuseColor * (1.0f - f0) * NoL / diffuseProbability;
        
        return float4(finalDiffuse * Frame.Diffuse, 0.0f);
    }
    else
    {
        float3 h_tan = SampleGGX(roughness, randomSeed);
        
        float3 h = mul(TBN, h_tan);
        
        float3 l = reflect(-v, h);        
        
        float NoL = dot(n, l);
        
        if (NoL <= 0.0f)
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        NoL = saturate(NoL);
     
        float NoH = saturate(dot(n, h));
        float VoH = saturate(dot(v, h));
                        
        float D = D_GGX(NoH, roughness);
        float V = V_SmithGGXCorrelatedFast(NoV, NoL, roughness);
        float3 F = F_Schlick(VoH, f0);
        
        // specular BRDF
        float3 Fr = (D * V) * F;
        
        float4 bounceColor = TraceRayIndirect(Scene, position + (GN * GN_OFFSET), l, depth, randomSeed) * SpecularAO(ao);
        
        float3 finalSpecular = Fr * bounceColor.rgb * NoL / (1.0f - diffuseProbability);
        
        return float4(finalSpecular * Frame.Specular, bounceColor.a);
    }
}

#endif // SHADING_HLSI