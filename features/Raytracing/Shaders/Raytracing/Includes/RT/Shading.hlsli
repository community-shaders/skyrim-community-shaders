#ifndef SHADING_HLSI
#define SHADING_HLSI

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"

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

float3 LambertianDirectD(in float3 position, in float3 normal, in float3 albedo, in Light light, inout uint randomSeed)
{
    float3 L = normalize(light.Vector);
 
    float NdotL = saturate(dot(normal, L)) * TraceRayShadow(Scene, position, L);
            
    return NdotL * light.Color * albedo; 
}

float3 LambertianDirectP(in float3 position, in float3 n, in float3 albedo, in LightData lightData, inout uint randomSeed)
{
    uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);

    uint lightID = lightData.GetID(lightIdx);
    
    Light light = Lights[lightID];
        
    float3 l = (light.Vector - position);
    float dist = length(l);      
    l /= dist;
    
    float atten = LinearAtten(dist, light.Range);
    //float atten = InverseSquareAtten(dist, light.Range); // This requires all lights to be ISL enabled
            
    float NdotL= saturate(dot(n, l)) * atten;
    NdotL *= float(lightData.Count) * TraceRayShadowFinite(Scene, position, l, dist);
            
    return NdotL * light.Color * albedo; // (albedo / Math::PI)
}

float4 LambertianIndirect(float3 position, float3 n, float3 albedo, uint depth, inout uint randomSeed)
{
    float3 tangentSample = SampleCosineHemisphere(randomSeed);
    float3 direction = TangentToWorld(n, tangentSample);
            
    float4 radiance = TraceRay(Scene, position, direction, depth, randomSeed);
    
    float NoL = saturate(dot(n, direction));    
    
    return float4(radiance.rgb * albedo * NoL * Frame.Diffuse, radiance.a);
}

float3 GGXDirect(in float3 l, in float3 n, in float3 v, in float3 albedo, in float roughness, in float metalness)
{
    float NoL = saturate(dot(n, l));
     
    if (NoL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 h = normalize(v + l);
        
    float NoH = saturate(dot(n, h));
    float NoV = clamp(dot(n, v), 1e-5f, 1.0f);
    float VoH = clamp(dot(v, h), 1e-5f, 1.0f);

    float D = D_GGX(NoH, roughness);
    float V = V_SmithGGXCorrelatedFast(NoV, NoL, roughness);
    float3 F = F_Schlick(VoH, F0(albedo, metalness));
    
    // specular BRDF
    float3 Fr = (D * V) * F;

    float3 diffuseAlbedo = (1.0 - metalness) * albedo;
    //float3 Fd = diffuseAlbedo / Math::PI;
    
    return (diffuseAlbedo + Fr) * NoL;
}

float3 GGXDirectD(in float3 position, in float3 n, in float3 v, in float3 albedo, in float roughness, in float metalness, in Light light, inout uint randomSeed)
{
    float3 l = light.Vector;

    float3 direct = GGXDirect(l, n, v, albedo, roughness, metalness) * light.Color;

    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.025f));  
        direct *= TraceRayShadow(Scene, position, lr);
    }

    return direct;
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
    
    float atten = LinearAtten(dist, light.Range);
    //float atten = InverseSquareAtten(dist, light.Range); // This requires all lights to be ISL enabled
    
    float3 direct = GGXDirect(l, n, v, albedo, roughness, metalness) * atten * light.Color;

    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.05f));        
        direct *= TraceRayShadowFinite(Scene, position, lr, dist);
    }
    
    return direct * float(lightData.Count);
}

float4 GetRadiance(in float3 position, in float3 direction, in uint depth, inout uint randomSeed)
{
#if defined(SHARC)
    if (!Frame.SHaRCUpdatePass) {
        SharcParameters sharcParameters = GetSharcParameters();
        
        SharcHitData sharcHitData;
        {
            sharcHitData.positionWorld = position;
            sharcHitData.normalWorld = direction;
        }       
        
        /*uint gridLevel = HashGridGetLevel(position, sharcParameters.gridParameters);
        float voxelSize = HashGridGetVoxelSize(gridLevel, gridParameters);
        
        bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);*/
        
        float3 sharcRadiance;   
        if (SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
            return sharcRadiance;
    } 
#endif
    
    return TraceRay(Scene, position, direction, depth, randomSeed);
}
#if defined(SHARC) && defined(SHARC_UPDATE)
float4 GGXIndirect(in float3 position, in float3 GN, float3x3 TBN, in float3 V, in float3 albedo, in float roughness, in float metalness, in float ao, in uint depth, SharcState sharcState, inout uint randomSeed)
#else
float4 GGXIndirect(in float3 position, in float3 GN, float3x3 TBN, in float3 V, in float3 albedo, in float roughness, in float metalness, in float ao, in uint depth, inout uint randomSeed)
#endif
{  
    float3 N = TBN[2];
    float3 T = TBN[0];
    float3 B = TBN[1];
    
    float3 f0 = F0(albedo, metalness);
  
    float3 diffuseAlbedo = albedo;
    
    float NoV = saturate(dot(N, V));
    
    bool isSpecularRay = false;
    bool isDeltaSurface = roughness == 0;
    float specular_PDF;
    float3 BRDF_over_PDF;
    float overall_PDF;
    float3 direction;
    
    {
        float3 specularDirection;
        float3 specular_BRDF_over_PDF;
        {
            float3 Ve = float3(dot(V, T), dot(V, B), dot(V, N));

            float3 He = SampleGGX_VNDF(Ve, roughness, randomSeed);
            float3 H = isDeltaSurface ? N : mul(He, TBN);
            specularDirection = reflect(-V, H);

            float HoV = saturate(dot(H, V));           
            float3 F = Schlick_Fresnel(f0, HoV);
            float G1 = isDeltaSurface ? 1.0 : (NoV > 0) ? G1_Smith(roughness, NoV) : 0;
            specular_BRDF_over_PDF = F * G1;
        }

        float3 diffuseDirection;
        float diffuse_BRDF_over_PDF;
        {
            float3 localDirection = SampleCosineHemisphere(randomSeed);
            diffuseDirection = mul(localDirection, TBN);
            diffuse_BRDF_over_PDF = 1.0;
        }

        specular_PDF = saturate(CalcLuminance(specular_BRDF_over_PDF) /
            CalcLuminance(specular_BRDF_over_PDF + diffuse_BRDF_over_PDF * diffuseAlbedo));

        isSpecularRay = Random(randomSeed) < specular_PDF;

        if (isSpecularRay)
        {
            direction = specularDirection;
            BRDF_over_PDF = specular_BRDF_over_PDF / specular_PDF;
        }
        else
        {
            direction = diffuseDirection;
            BRDF_over_PDF = diffuse_BRDF_over_PDF / (1.0 - specular_PDF);
        }

        /*const float specularLobe_PDF = ImportanceSampleGGX_VNDF_PDF(roughness, N, V, direction);
        const float diffuseLobe_PDF = saturate(dot(direction, N)) / Math::PI;

        // For delta surfaces, we only pass the diffuse lobe to ReSTIR GI, and this pdf is for that.
        overall_PDF = isDeltaSurface ? diffuseLobe_PDF : lerp(diffuseLobe_PDF, specularLobe_PDF, specular_PDF);*/
    }

    if (dot(GN, direction) <= 0.0)
        return float4(0f, 0f, 0f, 0f);   
    
    float4 radiance = GetRadiance(position, direction, depth, randomSeed);    
    
#if defined(SHARC) && defined(SHARC_UPDATE)
    if (Frame.SHaRCUpdatePass) {
        SharcParameters sharcParameters = GetSharcParameters();
    
        if (radiance.a < 0.0f) {
            SharcUpdateMiss(sharcParameters, sharcState, radiance.rgb);
        } else {
            SharcHitData sharcHitData;
            {
                sharcHitData.positionWorld = position;
                sharcHitData.normalWorld = GN;
            }
    
            SharcUpdateHit(sharcParameters, sharcState, sharcHitData, radiance.rgb, Random(randomSeed));
        } 
    
        return radiance;
    }
#endif  
    
    float3 diffuse = isSpecularRay ? 0.0 : radiance.rgb * BRDF_over_PDF * (DiffuseAO(diffuseAlbedo, ao) * Frame.Diffuse);
    float3 specular = isSpecularRay ? radiance.rgb * BRDF_over_PDF * (SpecularAO(NoV, roughness, ao, f0) * Frame.Specular): 0.0;    
    
    return float4(diffuse * diffuseAlbedo + specular, radiance.a);
}

#endif // SHADING_HLSI