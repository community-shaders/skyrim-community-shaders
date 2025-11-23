#ifndef SHADING_HLSI
#define SHADING_HLSI

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

#include "RaytracedGI/Includes/Types.hlsli"
#include "RaytracedGI/Includes/Registers.hlsli"
#include "RaytracedGI/Includes/Common.hlsli"
#include "RaytracedGI/Includes/RT/CommonRT.hlsli"
#include "RaytracedGI/Includes/RT/Rays.hlsli"

#include "RaytracedGI/Includes/RT/microfacetBRDFUtils.hlsli"

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

float3 LambertianDirect(in float3 position, in float3 normal, in float3 albedo, in LightData lightData, inout uint randomSeed)
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
    float3 tangentSample = TangentSample(randomSeed);
    float3 randomDirection = TangentToWorld(normal, tangentSample);
            
    float3 bounceColor = TraceRayIndirect(Scene, position, randomDirection, depth, randomSeed).rgb;
    
    return bounceColor * albedo * Frame.Diffuse;
}

float3 GGXDirectP(in float3 position, in float3 normal, in float3 view, in float3 albedo, in float3 specular, in float roughness, in LightData lightData, inout uint randomSeed)
{
    if (lightData.Count == 0) return float3(0, 0, 0);
    
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 L = (light.Vector - position);
    float dist = length(L);      
    L /= dist;
         
    float NdotL = saturate(dot(normal, L));
	float NdotV = saturate(dot(normal, view));
    
    if (NdotL <= 0.0 || NdotV <= 0.0) return 0;       
        
	float3 H = normalize(view + L);
	float NdotH = saturate(dot(normal, H));
    float VdotH = saturate(dot(view, H));
    
    float D = D_GGX(roughness, NdotH);
    float G = Vis_Smith(roughness, NdotV, NdotL);
    float3 F = F_Schlick(specular, VdotH);
    
    float3 diffBRDF = (albedo / Math::PI) * (1.0f - F);  
    
    //float atten = InverseSquareAtten(dist, light.Range); // This requires all lights to be ISL enabled
    float atten = LinearAtten(dist, light.Range);    
    
    float shadow = 1.0f;
    
    if (NdotL >= 0)
        shadow = TraceRayShadowFinite(Scene, position, L, dist);
    
    float3 radiance = light.Color * atten * shadow;
    
    float weight = float(lightData.Count);
    
    float3 specBRDF = D * G * F / max(4 * NdotV, EPSILON_DIVISION);
    
    return radiance * weight * (diffBRDF * NdotL + specBRDF) * Frame.Diffuse;
}

float3 GGXDirectD(in float3 position, in float3 normal, in float3 view, in float3 albedo, in float3 specular, in float roughness, in Light light)
{
    float3 L = light.Vector;

    float NdotL = saturate(dot(normal, L));
	float NdotV = saturate(dot(normal, view));
    
    if (NdotL <= 0.0 || NdotV <= 0.0) return 0;       
        
	float3 H = normalize(view + L);
	float NdotH = saturate(dot(normal, H));
    float VdotH = saturate(dot(view, H));
    
    float D = D_GGX(roughness, NdotH);
    float G = Vis_Smith(roughness, NdotV, NdotL);
    float3 F = F_Schlick3(specular, VdotH);
    
    float3 diffBRDF = (albedo / Math::PI) * (1.0f - F);  

    float3 radiance = light.Color * TraceRayShadow(Scene, position, L);

    float3 specBRDF = D * G * F / max(4 * NdotV, EPSILON_DIVISION);
    
    return radiance * (diffBRDF * NdotL + specBRDF) * Frame.Diffuse;
}

float4 GGXIndirect(in float3 position, in float3 GN, in float3 N, in float3 V, in float3 albedo, in float3 specular, in float roughness, in float metalness, in uint depth, inout uint randomSeed)
{ 
    float NdotV = dot(N, V);
    
    if (NdotV <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);  
    
    float diffuseProbability = DiffuseProbability(albedo, specular, metalness, roughness, NdotV);
    bool chooseDiffuse = (Random(randomSeed) < diffuseProbability);
    
    if (chooseDiffuse)
    {
        float3 tangentSample = TangentSample(randomSeed);
        float3 L = TangentToWorld(N, tangentSample);

        if (dot(GN, L) <= 0.0f) 
            return float4(0.0f, 0.0f, 0.0f, 0.0f);        
               
        float3 bounceColor = TraceRayIndirect(Scene, position, L, depth, randomSeed).rgb;
    
        return float4((bounceColor * albedo / diffuseProbability) * Frame.Diffuse, 0.0f);
    }
    else
    {
        float2 alpha = float2(roughness * roughness, roughness * roughness); // Just a test
        
        float3 Ht = GGXSample(randomSeed, alpha);

        float3 H = TangentToWorld(N, Ht);
        float3 L = reflect(-V, H);

        if (dot(GN, L) <= 0.0f) 
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        float NdotL = max(dot(N, L), EPSILON_DOT_CLAMP);
        
        //if (NdotL <= 0.0f)
        //    return float4(0.0f, 0.0f, 0.0f, 0.0f);            
        
   	    float NdotH = max(dot(N, H), EPSILON_DOT_CLAMP); 
        
        //if (NdotH <= 0.0f)
        //    return float4(0.0f, 0.0f, 0.0f, 0.0f);           
        
	    float LdotH = max(dot(L, H), EPSILON_DOT_CLAMP);
        
        //if (LdotH <= 0.0f)
        //    return float4(0.0f, 0.0f, 0.0f, 0.0f);            
        
	    float VdotH = max(dot(V, H), EPSILON_DOT_CLAMP);      
                  
        float4 bounceColor = TraceRayIndirect(Scene, position, L, depth, randomSeed);        
        
        float D = D_GGX(roughness, NdotH);
        float G = Vis_Smith(roughness, NdotV, NdotL);
        
        //float F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
        float3 F = F_Schlick3(specular, VdotH);
        
        float3 ggxTerm = D * G * F / max(4 * NdotL * NdotV, EPSILON_DOT_CLAMP);
    
        float ggxProb = D * NdotH / (4 * LdotH);
        
        return float4((NdotL * bounceColor.rgb * ggxTerm / (ggxProb * (1.0f - diffuseProbability))) * Frame.Specular, bounceColor.a);
    }
}

float4 GGXIndirect2(in float3 position, in float3 geomNormal, in float3 N, in float3 V, in float3 albedo, in float3 specular, in float roughness, in float metalness, in uint depth, inout uint randomSeed)
{ 
    float NdotV = saturate(dot(N, V));
    
    float diffuseProbability = DiffuseProbability(albedo, specular, metalness, roughness, NdotV);
    bool chooseDiffuse = (Random(randomSeed) < diffuseProbability);
    
    if (chooseDiffuse)
    {
        float3 tangentSample = TangentSample(randomSeed);
        float3 L = TangentToWorld(N, tangentSample);
            
        //if (dot(geomNormal, L) <= 0.0f) return float3(0, 0, 0);
        
        float3 bounceColor = TraceRayIndirect(Scene, position, L, depth, randomSeed).rgb;
    
        return float4((bounceColor * albedo / diffuseProbability) * Frame.Diffuse, 0.0f);
    }
    else
    {
        float2 alpha = float2(roughness * roughness, roughness * roughness); // Just a test
        
        float3 Ht = GGXSample(randomSeed, alpha);

        float3 H = TangentToWorld(N, Ht);
        float3 L = reflect(-V, H);

        //if (dot(geomNormal, L) <= 0.0f) return float3(0, 0, 0);
        
        float NdotL = dot(N, L);
   	    float NdotH = dot(N, H); 
	    float LdotH = dot(L, H);
	    float VdotH = dot(V, H);
        
        if (NdotL <= 0.0f || NdotV <= 0.0f || NdotH <= 0.0f || LdotH <= 0.0f)
            return float4(0.0f, 0.0f, 0.0f, 0.0f);    
        
        float4 bounceColor = TraceRayIndirect(Scene, position, L, depth, randomSeed);               
        
        float D = D_GGX_Ani(alpha, H);
        float G = Vis_Smith_Ani(alpha, V, L);
        
        /*float anisotropy = (alpha.y - alpha.x) / max(alpha.y + alpha.x, 1e-6f);
        
        float F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
        
        float3 F = F_Schlick2(F0, VdotH);
        
        float3 F0 = ComputeF0Aniso(baseColor, metallic, anisotropy);
        float3 F = F_SchlickAniso(F0x, F0y, dot(V,H), dot(H,tangent), dot(H,bitangent));  */      
        
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);     
        float3 F = F_Schlick2(F0, VdotH);       
        
        float3 ggxTerm = D * G * F / max(4 * NdotL * NdotV, EPSILON_DOT_CLAMP);
    
        float ggxProb = (D * NdotH) / (4 * LdotH); 
        
        return float4((NdotL * bounceColor.rgb * ggxTerm / (ggxProb * (1.0f - diffuseProbability))) * Frame.Specular, bounceColor.a);
    }
}

#endif // SHADING_HLSI