#ifndef SHADING_HLSL
#define SHADING_HLSL

#include "Common/Game.hlsli"
#include "Common/BRDF.hlsli"

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
    
    float atten = VanillaSquaredAtten(dist, light.Range);
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

float3 EvalDefaultBRDF(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NoL = saturate(dot(surface.Normal, l));
     
    if (NoL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 H = normalize(brdfContext.ViewDirection + l);
        
    float NoH = saturate(dot(surface.Normal, H));
    float VoH = clamp(dot(brdfContext.ViewDirection, H), 1e-5f, 1.0f);

    float D = BRDF::D_GGX(surface.Roughness, NoH);
    float Vis = BRDF::Vis_SmithJointApprox(surface.Roughness, max(1e-5f, brdfContext.NdotV), NoL);
    float3 F = BRDF::F_Schlick(surface.F0, VoH);
    
    // specular BRDF
    float3 Fr = (D * Vis) * F * Frame.Specular;
    float3 Fd = BRDF::Diffuse_Burley(surface.Roughness, brdfContext.NdotV, NoL, VoH) * surface.DiffuseAlbedo * Frame.Diffuse;
    
    return (Fd + Fr) * NoL;
}

float3 EvalDirectionalLight(in Surface surface, in BRDFContext brdfContext, in Light light, inout uint randomSeed)
{
    float3 direct = EvalDefaultBRDF(light.Vector, surface, brdfContext) * light.Color;

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
    float atten = InverseSquareAtten(dist, light.Range * 64) * 2560; // This requires all lights to be ISL enabled
    
    float3 direct = EvalDefaultBRDF(l, surface, brdfContext) * atten * light.Color * float(lightData.Count);

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
    float3 H = 0;
    float NdotL = 0;

    const float specularProb = MonteCarlo::GetSpecularBrdfProbability(surface, V, surface.Normal);
    const bool isSpecular = Random(randomSeed) < specularProb;

    float lobePDF = isSpecular ? specularProb : (1.0f - specularProb);

    float3 brdf = 0.0f;
    float brdfPdf = 0.0f;

    if (!isSpecular)
    {
        float3 localDirection = SampleCosineHemisphere(randomSeed);
        L = surface.Mul(localDirection);
        H = normalize(V + L);
        NdotL = saturate(dot(surface.Normal, L));
        brdf = BRDF::Diffuse_Burley(surface.Roughness, brdfContext.NdotV, NdotL, saturate(dot(H, V))) * NdotL * surface.DiffuseAlbedo;
        brdfPdf = NdotL / Math::PI;
        brdfWeight = Frame.Diffuse * brdf / max(brdfPdf * lobePDF, 1e-7f);
    }
    else
    {
        float3 Ve = float3(
                dot(brdfContext.ViewDirection, surface.Tangent), 
                dot(brdfContext.ViewDirection, surface.Bitangent), 
                dot(brdfContext.ViewDirection, surface.Normal)
            );

        const float alpha = surface.Roughness * surface.Roughness;
        const float alpha2 = alpha * alpha;

        float2 E = Get2D(randomSeed);
        E.x = MonteCarlo::RescaleRandomNumber(E.x, specularProb, 1.0f);
        // float2 Xi = MonteCarlo::Hammersley(0, 1, random2D);
        // float3 He = MonteCarlo::SampleGGX_VNDF(Ve, alpha, randomSeed);
        float4 HePDF = MonteCarlo::ImportanceSampleVisibleGGX(E, alpha, Ve);
        float3 He = HePDF.xyz;
        float3 Le = reflect(-Ve, He);
        const float2 GGXResult = MonteCarlo::GGXEvalReflection(Le, Ve, He, alpha);
        H = surface.Mul(He);
        L = reflect(-V, H);

        NdotL = saturate(dot(surface.Normal, L));
        float VdotH = saturate(dot(H, V));
        float NdotH = saturate(dot(surface.Normal, H));
        float3 F = BRDF::F_Schlick(surface.F0, VdotH);

        brdf = F * GGXResult.x;
        brdfPdf = GGXResult.y;
        brdfWeight = Frame.Specular * brdf / max(brdfPdf * lobePDF, 1e-7f);
    }

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
float3 SampleRadiance(in Surface surface, in BRDFContext brdfContext, in Instance instance, in Material material, inout uint randomSeed)
{
    float3 radiance = surface.Emissive * Frame.Emissive;
    
#if defined(LAMBERT)
    radiance += LambertianDirectD(surface, Frame.Directional, randomSeed);
    radiance += LambertianDirectP(surface, instance.LightData, randomSeed);
#else
    radiance += EvalDirectionalLight(surface, brdfContext, Frame.Directional, randomSeed);
    radiance += EvalPointLight(surface, brdfContext, instance.LightData, randomSeed);
#endif    
    
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