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
    float3 randomDirection = SampleHemisphere(normal, tangentSample);
            
    float3 bounceColor = TraceRayIndirect(Scene, position, randomDirection, depth, randomSeed);
    
    return bounceColor * albedo * Frame.Diffuse;
}

float Vis_Smith(float roughness, float NdotV, float NdotL)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float Vis_SmithV = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    float Vis_SmithL = NdotL + sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
    return max(Vis_SmithV * Vis_SmithL, EPSILON_DIVISION);
}

float3 GGXDirect(in float3 position, in float3 normal, in float3 view, in float3 albedo, in float3 specular, in float roughness, in LightData lightData, inout uint randomSeed)
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
            
    float NdotL= saturate(dot(normal, lightVector));
    float shadow = TraceRayShadowFinite(Scene, position, lightVector, lightDistance * M_TO_GAME_UNIT);
           
	float3 H = normalize(view + lightVector);
	float NdotH = saturate(dot(normal, H));
	float LdotH = saturate(dot(lightVector, H));
	float NdotV = saturate(dot(normal, view));
    
    float satNdotL = clamp(NdotL, EPSILON_DOT_CLAMP, 1);
    float satNdotV = saturate(abs(NdotV) + EPSILON_DOT_CLAMP);
    
    float D = BRDF::D_GGX(roughness, NdotH);
    float G = Vis_Smith(roughness, NdotV, NdotL);
    float3 F = BRDF::F_Schlick(specular, LdotH);

    float3 specBRDF = D * G * F;
    float2 specularBRDF = BRDF::EnvBRDF(roughness, satNdotV);
    specBRDF *= 1 + specBRDF * (1 / (specularBRDF.x + specularBRDF.y) - 1);
    
    float3 diffBRDF = albedo / Math::PI;
    
    float3 radiance = light.Color * attenuation * fade * fade;
    
    float weight = lightData.Count;
    
    float3 ggxTerm = D * G * F / (4 * satNdotV );
    
    return shadow * radiance * (ggxTerm + diffBRDF) * NdotL * weight * Frame.Diffuse;
}

float3 GGXIndirect(in float3 position, in float3 geomNormal, in float3 normal, in float3 view, in float3 albedo, in float3 specular, in float roughness, in uint depth, inout uint randomSeed)
{ 
    float probDiffuse = ProbabilityToSampleDiffuse(albedo, specular);
    float chooseDiffuse = (Random(randomSeed) < probDiffuse);
    
    float NdotV = saturate(dot(normal, view));
    
    if (chooseDiffuse)
    {
        float3 tangentSample = TangentSample(randomSeed);
        float3 L = SampleHemisphere(normal, tangentSample);
            
        float3 bounceColor = TraceRayIndirect(Scene, position, L, depth, randomSeed);
    
        return (bounceColor * albedo / probDiffuse) * Frame.Diffuse;
    }
    else
    {
        float3 tangentSample = TangentSampleScaled(randomSeed, roughness);
        float3 H = SampleHemisphere(normal, tangentSample);       
        
        float3 L = normalize(2.f * dot(view, H) * H - view);
        
        if (dot(geomNormal, L) <= 0.0f) return float3(0, 0, 0);
        
        float3 bounceColor = TraceRayIndirect(Scene, position, L, depth, randomSeed);
        
        float NdotL = saturate(dot(normal, L));
   	    float NdotH = saturate(dot(normal, H));
	    float LdotH = saturate(dot(L, H));

        float D = BRDF::D_GGX(roughness, NdotH);
        float G = Vis_Smith(roughness, NdotV, NdotL);
        float3 F = BRDF::F_Schlick(specular, LdotH);         
        
        float3 ggxTerm = D * G * F / max(4 * NdotL * NdotV, EPSILON_DOT_CLAMP);     
        
        float ggxProb = D * NdotH / (4 * LdotH);
        
        return (NdotL * bounceColor * ggxTerm / (ggxProb * (1.0f - probDiffuse))) * Frame.Specular;
    }
}

#endif // SHADING_HLSI