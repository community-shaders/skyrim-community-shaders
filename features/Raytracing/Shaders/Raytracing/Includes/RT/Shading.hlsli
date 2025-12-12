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
    float LoH = saturate(dot(l, h));
    float NoV = abs(dot(n, v)) + 1e-5;
     
    float3 F = F_Schlick(LoH, F0(albedo, metalness));
        
    float D = D_GGX(NoH, roughness);
    float V = V_SmithGGXCorrelatedFast(NoV, NoL, roughness);
        
    // specular BRDF
    float3 Fr = (D * V) * F;

    float3 diffuseColor = (1.0 - metalness) * albedo;
    float3 Fd = diffuseColor * Fd_Lambert();

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

float4 GGXIndirect(in float3 position, in float3 GN, float3x3 TBN, in float3 v, in float3 albedo, in float roughness, in float metalness, in uint depth, inout uint randomSeed)
{  
    float3 n = TBN[2];
    
    float3 f0 = F0(albedo, metalness);
  
    float NoV = abs(dot(n, v)) + 1e-5;       
    
    float diffuseProbability = DiffuseProbability(roughness, metalness, f0, NoV);
    bool chooseDiffuse = (Random(randomSeed) < diffuseProbability);
    
    if (chooseDiffuse)
    {
        float3 l_tan = CosineSampleHemisphere(randomSeed);
        float3 l = normalize(mul(TBN, l_tan));

        //if (dot(GN, l) <= 0.0f) 
        //    return float4(0.0f, 0.0f, 0.0f, 0.0f);
               
        float3 bounceColor = TraceRayIndirect(Scene, position + (GN * GN_OFFSET), l, depth, randomSeed).rgb;
    
        float NoL = saturate(dot(n, l)); 
        
        float3 finalDiffuse = bounceColor * albedo * NoL / diffuseProbability;
        
        return float4(finalDiffuse * Frame.Diffuse, 0.0f);
    }
    else
    {
        float3 h_tan = SampleGGX(roughness, randomSeed);
        
        float3 h = normalize(mul(TBN, h_tan));
        
        float3 l = normalize(reflect(-v, h));        
        
        float NoL = saturate(dot(n, l));
     
        float NoH = saturate(dot(n, h));
        float LoH = saturate(dot(l, h));
        
        float4 bounceColor = TraceRayIndirect(Scene, position + (GN * GN_OFFSET), l, depth, randomSeed);        
                    
        float D = D_GGX(NoH, roughness);
        float V = V_SmithGGXCorrelatedFast(NoV, NoL, roughness);
        float3 F = F_Schlick(LoH, f0);
        
        // specular BRDF
        float3 Fr = (D * V) * F;

        float specularWeight = 1.0f - diffuseProbability;
        
        float3 finalSpecular = Fr * bounceColor.rgb * NoL / specularWeight;
        
        return float4(finalSpecular * Frame.Specular, bounceColor.a);
    }
}

float4 GGXIndirect3(in float3 position, in float3 GN, float3x3 TBN, in float3 v, in float3 albedo, in float3 specular, in float roughness, in float metalness, in uint depth, inout uint randomSeed)
{  
    float3 n = TBN[2];
    
    float3 f0 = F0(albedo, metalness);
  
    float NoV = abs(dot(n, v)) + 1e-5;       
    
    float diffuseProbability = DiffuseProbability(roughness, metalness, f0, NoV);
    bool chooseDiffuse = (Random(randomSeed) < diffuseProbability);
    
    if (chooseDiffuse)
    {
        float3 tangentSample = CosineSampleHemisphere(randomSeed);
        float3 l = normalize(mul(TBN, tangentSample));

        float NoL = saturate(dot(n, l));
        //if (dot(GN, l) <= 0.0f) 
        //    return float4(0.0f, 0.0f, 0.0f, 0.0f);
               
        float3 bounceColor = TraceRayIndirect(Scene, position + (GN * GN_OFFSET), l, depth, randomSeed).rgb;
    
        return float4((bounceColor * albedo * NoL / diffuseProbability) * Frame.Diffuse, 0.0f);
    }
    else
    {
       /* float3 v_tan = float3(
            dot(v, TBN[0]),
            dot(v, TBN[1]),
            dot(v, TBN[2])
        );
        
        float3 l_tan = SampleGGXVNDF_Direct(v_tan, randomSeed, roughness);
        
        float3 l = normalize(mul(TBN, l_tan));*/

        float3 h_tan = GGXSample(randomSeed, roughness);
        
        float3 h = normalize(mul(TBN, h_tan));
        
        float3 l = normalize(reflect(v, h));        
        
        float NoL = saturate(dot(n, l));
     
        //if (NoL <= 0.0f) 
        //    return float4(0.0f, 0.0f, 0.0f, 0.0f);          
        
        //float3 h = normalize(v + l);
        
        float NoH = saturate(dot(n, h));
        float LoH = saturate(dot(l, h));
        
        float4 bounceColor = TraceRayIndirect(Scene, position + (GN * GN_OFFSET), l, depth, randomSeed);        
                    
        float D = D_GGX(NoH, roughness);
        float V = V_SmithGGXCorrelatedFast(NoV, NoL, roughness);
        float3 F = F_Schlick(LoH, f0);
        
        // specular BRDF
        float3 Fr = (D * V) * F;

        float specularWeight = 1.0f - diffuseProbability;
        
        float3 finalSpecular = Fr * bounceColor.rgb * NoL / specularWeight;
        
        return float4(finalSpecular * Frame.Specular, bounceColor.a);
    }
}

#endif // SHADING_HLSI