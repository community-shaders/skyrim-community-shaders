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
    float NdotL = saturate(dot(surface.Normal, l));
     
    if (NdotL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    // Diffuse is meant to be very light (and used with DDGI), so I don't see much point in using a different diffuse or shading model here
    return surface.DiffuseAlbedo * NdotL * BRDF::Diffuse_Lambert();
}

float3 EvalDefaultBSDF(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NdotL = saturate(dot(surface.Normal, l));
     
    if (NdotL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 H = normalize(brdfContext.ViewDirection + l);
        
    float NdotH = saturate(dot(surface.Normal, H));
    float VdotH = clamp(dot(brdfContext.ViewDirection, H), 1e-5f, 1.0f);
    float VdotL = saturate(dot(brdfContext.ViewDirection, l));
    
    float D = BRDF::D_GGX(surface.Roughness, NdotH);
    float Vis = BRDF::Vis_SmithJointApprox(surface.Roughness, max(1e-5f, brdfContext.NdotV), NdotL);
    float3 F = BRDF::F_Schlick(surface.F0, VdotH);
    
    // specular BRDF
    float3 Fr = (D * Vis) * F;
    
    float3 Fd = surface.DiffuseAlbedo
    * Diffuse(surface.Roughness, surface.Normal, brdfContext.ViewDirection, l, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH) 
    * ShadowTerminatorTerm(l, surface.Normal, surface.GeomNormal);
    
    return (Fd + Fr) * NdotL;
}

#if defined(FULL_MATERIAL)
float3 EvalFuzzBSDF(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NdotL = saturate(dot(surface.Normal, l));
     
    if (NdotL <= 0.0f) 
        return float3(0.0f, 0.0f, 0.0f);
        
    float3 H = normalize(brdfContext.ViewDirection + l);
        
    float NdotH = saturate(dot(surface.Normal, H));
    float VdotH = clamp(dot(brdfContext.ViewDirection, H), 1e-5f, 1.0f);
    float VdotL = saturate(dot(brdfContext.ViewDirection, l));
    
    float D = BRDF::D_GGX(surface.Roughness, NdotH);
    float Vis = BRDF::Vis_SmithJointApprox(surface.Roughness, max(1e-5f, brdfContext.NdotV), NdotL);
    float3 F = BRDF::F_Schlick(surface.F0, VdotH);
    
    // specular BRDF
    float3 Fr = (D * Vis) * F;
    
    float3 Fd = surface.DiffuseAlbedo
    * Diffuse(surface.Roughness, surface.Normal, brdfContext.ViewDirection, l, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH) 
    * ShadowTerminatorTerm(l, surface.Normal, surface.GeomNormal);

    float Efuzz = (0.526422 / ((-0.227114 + surface.Roughness) * (-0.968835 + surface.Roughness) * ((5.38869 - 20.2835 * brdfContext.NdotV) * surface.Roughness) - (-1.18761 - ((2.58744 - brdfContext.NdotV) * brdfContext.NdotV)))) + 0.0615456;
    float Dfuzz = BRDF::D_Charlie(surface.Roughness, NdotH);
    float Gfuzz = BRDF::Vis_Neubelt(brdfContext.NdotV, NdotL);
    float3 Ffuzz = surface.FuzzColor * Dfuzz * Gfuzz;

    return ((Fd + Fr) * lerp(1, 1 - Efuzz, surface.FuzzWeight) + Ffuzz * surface.FuzzWeight) * NdotL;
}
#endif

float3 EvalLight(in float3 l, in Surface surface, in BRDFContext brdfContext, in Material material)
{
#if LIGHTEVAL_MODE == LIGHTEVAL_MODE_DIFFUSE
    return EvalDiffuse(l, surface, brdfContext); 
#else
#   if defined(FULL_MATERIAL)
    if ((material.PBRFlags & PBR::Flags::Fuzz) != 0)
        return EvalFuzzBSDF(l, surface, brdfContext);
    else
#   endif
    return EvalDefaultBSDF(l, surface, brdfContext);
#endif
}

float3 EvalDirectionalLight(in Surface surface, in BRDFContext brdfContext, in Light light, in Material material, inout uint randomSeed)
{
    float3 direct = EvalLight(light.Vector, surface, brdfContext, material) * light.Color;

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(light.Vector, SampleCosineHemisphereScaled(randomSeed, 0.025f));  
        direct *= TraceRayShadow(Scene, surface.Position, lr);
    }

    return direct;
}

float GetLightSampleWeight(Surface surface, Light light)
{
    float3 l = (light.Vector - surface.Position);
    float dist = length(l) * GAME_UNIT_TO_M;
    float atten = 1.0 / (1.0 + dist * dist);
    float intensity = max(light.Color.r, max(light.Color.g, light.Color.b));
    return atten * intensity;
}

float3 EvalPointLight(in Surface surface, in BRDFContext brdfContext, in LightData lightData, in Material material, inout uint randomSeed)
{
    if (lightData.Count == 0) 
        return float3(0, 0, 0);

    float lightWeight = float(lightData.Count);

#if defined(RIS)
    const uint candidateCount = 4;
    uint selectedLightID = 0;
    float totalWeight = 0.0f;
    float selectedWeight = 0.0f;

    for (uint i = 0; i < candidateCount; i++)
    {
        uint lightIdx = min(uint(Random(randomSeed) * lightData.Count), lightData.Count - 1);
        uint lightID = lightData.GetID(lightIdx);
        Light testLight = Lights[lightID];
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
        
    float3 l = (light.Vector - surface.Position);
    float dist = length(l);      
    l /= dist;
    
    // float atten = VanillaSquaredAtten(dist, light.Range);
    float atten = InverseSquareAtten(dist * GAME_UNIT_TO_M, light.Range * 64); // This is temporal
    
    float3 direct = EvalLight(l, surface, brdfContext, material) * atten * light.Color * lightWeight;

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        float3 lr = TangentToWorld(l, SampleCosineHemisphereScaled(randomSeed, 0.05f));        
        direct *= TraceRayShadowFinite(Scene, surface.Position, lr, dist);
    }
    
    return direct;
}

void SampleDefaultBSDF(in Surface surface, in BRDFContext brdfContext, inout uint randomSeed, out float3 direction, out float3 brdfWeight)
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

    float3 Fd = surface.DiffuseAlbedo * NdotL 
        * Diffuse(surface.Roughness, surface.Normal, V, L, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH) 
        * ShadowTerminatorTerm(L, surface.Normal, surface.GeomNormal);
    
    float3 Fr = F * MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, brdfContext.NdotV, VdotH, NdotH) * specularPdf;
#if GGX_ENERGY_CONSERVATION
    Fr *= BRDF::GGXEnergyConservationTerm(surface.F0, surface.Roughness, brdfContext.NdotV);
#endif

    pdf = (1.0f - specularProb) * diffusePdf + specularProb * specularPdf;

    brdf = Fd + Fr;

    brdfWeight = brdf / max(pdf, 1e-7f);

    direction = L;
}

#if defined(FULL_MATERIAL)
void SampleFuzzBSDF(in Surface surface, in BRDFContext brdfContext, inout uint randomSeed, out float3 direction, out float3 brdfWeight)
{
    const float3 V = brdfContext.ViewDirection;
    float3 L = 0;
    float NdotL = 0;

    brdfWeight = 0.0f;

    float specularProb = lerp(MonteCarlo::GetSpecularBrdfProbability(surface, V, surface.Normal), 1.0f, surface.Metallic);
    float Efuzz = (0.526422 / ((-0.227114 + surface.Roughness) * (-0.968835 + surface.Roughness) * ((5.38869 - 20.2835 * brdfContext.NdotV) * surface.Roughness) - (-1.18761 - ((2.58744 - brdfContext.NdotV) * brdfContext.NdotV)))) + 0.0615456;
    float fuzzProb = Efuzz * surface.FuzzWeight;
    specularProb *= 1 - fuzzProb;
    const float lobeRand = Random(randomSeed);

    float3 brdf = 0.0f;
    float pdf = 0.0f;
    float diffusePdf = 0.0f;
    float specularPdf = 0.0f;
    float fuzzPdf = 0.0f;

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
    if (lobeRand < specularProb)
    {
        He = MonteCarlo::SampleGGX_VNDF(Ve, alpha, randomSeed);
        Le = reflect(-Ve, He);
    }
    else
    {
        Le = SampleCosineHemisphere(randomSeed);
        He = normalize(Ve + Le);
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
    fuzzPdf = diffusePdf;

    float3 F = BRDF::F_Schlick(surface.F0, VdotH);

    float3 Fd = surface.DiffuseAlbedo * NdotL 
        * Diffuse(surface.Roughness, surface.Normal, V, L, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH) 
        * ShadowTerminatorTerm(L, surface.Normal, surface.GeomNormal);
    
    float3 Fr = F * MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, brdfContext.NdotV, VdotH, NdotH) * specularPdf;
#if GGX_ENERGY_CONSERVATION
    Fr *= BRDF::GGXEnergyConservationTerm(surface.F0, surface.Roughness, brdfContext.NdotV);
#endif

    // Fuzz
    const float Dfuzz = BRDF::D_Charlie(surface.Roughness, NdotH);
    const float Gfuzz = BRDF::Vis_Neubelt(NdotV, NdotL);
    float3 Ffuzz = surface.FuzzColor * Dfuzz * Gfuzz * NdotL;

    pdf = (1.0f - fuzzProb - specularProb) * diffusePdf + specularProb * specularPdf + fuzzProb * fuzzPdf;

    brdf = (Fd + Fr) * lerp(1, 1 - Efuzz, surface.FuzzWeight) + Ffuzz * surface.FuzzWeight;

    brdfWeight = brdf / max(pdf, 1e-7f);

    direction = L;
}
#endif

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
    float3 radiance;
    
    if (material.ShaderType == ShaderType::Lighting)
    {
        radiance = surface.Emissive * Frame.Emissive;
        
        /*if (material.PBRFlags & PBR::Flags::Subsurface)
        {
            // Do something expensive
        }*/          
    } else if (material.ShaderType == ShaderType::Effect)
    {
        radiance = surface.Emissive * Frame.Effect * Frame.Emissive;
    }
    else
    {
        radiance = surface.Emissive * Frame.Emissive;
    }
    
    radiance += EvalDirectionalLight(surface, brdfContext, Frame.Directional, material, randomSeed);
    radiance += EvalPointLight(surface, brdfContext, instance.LightData, material, randomSeed);    
    
    return radiance;
}

#endif // SHADING_HLSL