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

float3 EvalTransmissionBSDF(in float3 l, in Surface surface, in BRDFContext brdfContext, in bool isEnter)
{
    float materialIOR = max(surface.IOR, 1.0f);
    
    // Special case: IOR=1 means no refraction, light passes straight through
    if (abs(materialIOR - 1.0f) < 0.001f)
    {
        float NdotL = abs(dot(surface.Normal, l));
        if (NdotL > 0.0f)
            return surface.TransmissionColor * NdotL;
        return float3(0.0f, 0.0f, 0.0f);
    }
    
    float eta = isEnter ? (1.0f / materialIOR) : materialIOR;
    
    float NdotL = dot(surface.Normal, l);
    float NdotV = dot(surface.Normal, brdfContext.ViewDirection);

    bool isReflection = NdotL * NdotV > 0.0f;
    
    NdotL = abs(NdotL);
    if (NdotL <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);
    
    float3 H;
    float VdotH;
    
    if (isReflection)
    {
        H = normalize(brdfContext.ViewDirection + l);
        VdotH = clamp(dot(brdfContext.ViewDirection, H), 1e-5f, 1.0f);
    }
    else
    {
        H = -normalize(brdfContext.ViewDirection * eta + l);
        VdotH = abs(dot(brdfContext.ViewDirection, H));
    }
    
    float NdotH = saturate(dot(surface.Normal, H));
    
    float F = FresnelDielectric(eta, VdotH);
    float D = BRDF::D_GGX(surface.Roughness, NdotH);
    float Vis = BRDF::Vis_SmithJointApprox(surface.Roughness, max(1e-5f, abs(NdotV)), NdotL);
    
    if (isReflection)
    {
        float3 Fr = (D * Vis) * F;
        return Fr * NdotL;
    }
    else
    {
        float LdotH = abs(dot(l, H));
        float sqrtDenom = VdotH + eta * LdotH;
        float jacobian = (eta * eta * LdotH) / max(sqrtDenom * sqrtDenom, 1e-7f);

        float etaScale = eta * eta;
        float3 Ft = surface.TransmissionColor * (1.0f - F) * (D * Vis) * etaScale * jacobian;
        return Ft * NdotL;
    }
}

float3 EvalLight(in float3 l, in Surface surface, in BRDFContext brdfContext, in Material material)
{
#if LIGHTEVAL_MODE == LIGHTEVAL_MODE_DIFFUSE
    return EvalDiffuse(l, surface, brdfContext);
#else
    bool hasTransmission = any(surface.TransmissionColor) > 0.0f;
    if (hasTransmission)
    {
        bool isEnter = dot(brdfContext.ViewDirection, surface.GeomNormal) > 0.0f;
        return EvalTransmissionBSDF(l, surface, brdfContext, isEnter);
    }
#   if defined(FULL_MATERIAL)
    else if ((material.PBRFlags & PBR::Flags::Fuzz) != 0)
        return EvalFuzzBSDF(l, surface, brdfContext);
#   endif
    else
        return EvalDefaultBSDF(l, surface, brdfContext);
#endif
}

float3 EvalDirectionalLight(in Surface surface, in BRDFContext brdfContext, in DirectionalLight light, in Material material, inout uint randomSeed)
{
    light.Color = DirLightToLinear(light.Color);
    // Sun angular radius is ~0.00465 radians (~0.266 degrees)
    float cosSunDisk = cos(0.00465f);
    float3 lr = TangentToWorld(light.Vector, SampleConeUniform(randomSeed, cosSunDisk));
    float3 direct = EvalLight(lr, surface, brdfContext, material) * light.Color;

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

float3 EvalPointLight(in Surface surface, in BRDFContext brdfContext, in LightData lightData, in Material material, inout uint randomSeed)
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

    float3 direct = EvalLight(lr, surface, brdfContext, material) * atten * light.Color * light.Fade * lightWeight;

    [branch]
    if (any(direct > MIN_DIFFUSE_SHADOW))
    {
        direct *= TraceRayShadowFinite(Scene, surface, lr, dist, randomSeed);
    }

    return direct;
}

bool SampleDefaultBSDF(in Surface surface, in BRDFContext brdfContext, inout uint randomSeed, out float3 direction, out MonteCarlo::BRDFWeight brdfWeight)
{
    const float3 V = brdfContext.ViewDirection;
    float3 L = 0;
    float NdotL = 0;

    brdfWeight.diffuse = 0.0f;
    brdfWeight.specular = 0.0f;
    brdfWeight.transmission = 0.0f;

    const float specularProb = lerp(MonteCarlo::GetSpecularBrdfProbability(surface, V, surface.Normal), 1.0f, surface.Metallic);
    const bool isSpecular = Random(randomSeed) < specularProb;

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

#if defined(RAW_RADIANCE)
    float diffuseAlbedo = 1.0f;
#else
    float3 diffuseAlbedo = surface.DiffuseAlbedo;
#endif

    float3 Fd = diffuseAlbedo * NdotL
        * Diffuse(surface.Roughness, surface.Normal, V, L, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH)
        * ShadowTerminatorTerm(L, surface.Normal, surface.GeomNormal);

    float3 Fr = F * MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, brdfContext.NdotV, VdotH, NdotH) * specularPdf;
#if GGX_ENERGY_CONSERVATION
    Fr *= BRDF::GGXEnergyConservationTerm(surface.F0, surface.Roughness, brdfContext.NdotV);
#endif

    pdf = (1.0f - specularProb) * diffusePdf + specularProb * specularPdf;

    float pdfRCP = 1.0f / max(pdf, 1e-7f);

    brdfWeight.diffuse = Fd * pdfRCP;
    brdfWeight.specular = Fr * pdfRCP;

    direction = L;

    return isSpecular;
}

bool SampleTransmissionBSDF(in Surface surface, in BRDFContext brdfContext, in bool isEnter, inout uint randomSeed, out float3 direction, out MonteCarlo::BRDFWeight brdfWeight)
{
    const float3 V = brdfContext.ViewDirection;
    float3 N = surface.Normal;
    float3 L = 0;
    float NdotL = 0;

    brdfWeight.diffuse = 0.0f;
    brdfWeight.specular = 0.0f;
    brdfWeight.transmission = 0.0f;

    float materialIOR = max(surface.IOR, 1.0f);
    
    // Special case: IOR=1 means no refraction, light passes straight through
    if (abs(materialIOR - 1.0f) < 0.001f)
    {
        direction = -V;  // Light continues in same direction
        brdfWeight.transmission = surface.TransmissionColor;
        return false;  // Not specular, just transmission
    }
    
    float eta = isEnter ? (1.0f / materialIOR) : materialIOR;
    
    // Use raw NdotV for transmission (not saturated)
    float rawNdotV = dot(surface.Normal, V);

    direction = float3(0, 0, 0);

    float3 Ve = float3(
            dot(V, surface.Tangent),
            dot(V, surface.Bitangent),
            dot(V, surface.Normal)
        );

    const float alpha = surface.Roughness * surface.Roughness;
    const float alpha2 = alpha * alpha;
    
    float3 He = MonteCarlo::SampleGGX_VNDF(Ve, alpha, randomSeed);
    float3 H = surface.Mul(He);

    float VdotH = saturate(dot(Ve, He));
    float NdotH = saturate(dot(surface.Normal, H));

    float F = saturate(FresnelDielectric(eta, VdotH));

    float rnd = Random(randomSeed);
    float pdf = 0.0f;
    float3 Le = 0.0f;

    if (rnd < F)
    {
        Le = reflect(-Ve, He);
        L = surface.Mul(Le);
        NdotL = saturate(dot(surface.Normal, L));
        
        if (NdotL <= 0.0f)
            return false;
        
        float specularPdf = MonteCarlo::SampleGGXVNDFReflectionPdf(alpha, alpha2, NdotH, abs(rawNdotV), VdotH);
        pdf = F * specularPdf;
        
        float3 Fr = MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, abs(rawNdotV), VdotH, NdotH) * specularPdf;
        
#if GGX_ENERGY_CONSERVATION
        Fr *= BRDF::GGXEnergyConservationTerm(surface.F0, surface.Roughness, abs(rawNdotV));
#endif
        
        brdfWeight.specular = Fr / max(pdf, 1e-7f);
        direction = L;
        return true;
    }
    else
    {
        float cosThetaT;
        float3 T = refract(-Ve, He, eta);

        if (length(T) < 0.01f) {
            Le = reflect(-Ve, He);
            L = surface.Mul(Le);
            NdotL = saturate(dot(surface.Normal, L));
            
            if (NdotL <= 0.0f)
                return false;
            
            float specularPdf = MonteCarlo::SampleGGXVNDFReflectionPdf(alpha, alpha2, NdotH, abs(rawNdotV), VdotH);
            pdf = specularPdf;
            
            float3 Fr = MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, abs(rawNdotV), VdotH, NdotH) * specularPdf;
            brdfWeight.specular = Fr / max(pdf, 1e-7f);
            direction = L;
            return true;
        }

        Le = normalize(T);
        L = surface.Mul(Le);
        NdotL = abs(dot(surface.Normal, L));

        float TdotH = abs(dot(Le, He));
        float LdotH = abs(dot(L, H));

        float sqrtDenom = VdotH + eta * LdotH;
        float jacobian = (eta * eta * abs(LdotH)) / (sqrtDenom * sqrtDenom);

        float transmissionPdf = MonteCarlo::SampleGGXVNDFReflectionPdf(alpha, alpha2, NdotH, abs(rawNdotV), VdotH) * jacobian;
        pdf = (1 - F) * transmissionPdf;

        // Scale by eta^2 when exiting (eta > 1) to compensate for solid angle change
        // When entering (eta < 1), don't scale down to avoid darkening
        float etaScale = eta * eta;
        float3 transmissionWeight = surface.TransmissionColor * (1.0f - F) * etaScale * NdotL;
        
        float G = MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, abs(rawNdotV), abs(TdotH), NdotH);
        
        brdfWeight.transmission = transmissionWeight * G * transmissionPdf / max(pdf, 1e-7f);
        direction = L;
        return true;
    }
}

#if defined(FULL_MATERIAL)
bool SampleFuzzBSDF(in Surface surface, in BRDFContext brdfContext, inout uint randomSeed, out float3 direction, out MonteCarlo::BRDFWeight brdfWeight)
{
    const float3 V = brdfContext.ViewDirection;
    float3 L = 0;
    float NdotL = 0;

    brdfWeight.diffuse = 0.0f;
    brdfWeight.specular = 0.0f;
    brdfWeight.transmission = 0.0f;

    float specularProb = lerp(MonteCarlo::GetSpecularBrdfProbability(surface, V, surface.Normal), 1.0f, surface.Metallic);
    float Efuzz = (0.526422 / ((-0.227114 + surface.Roughness) * (-0.968835 + surface.Roughness) * ((5.38869 - 20.2835 * brdfContext.NdotV) * surface.Roughness) - (-1.18761 - ((2.58744 - brdfContext.NdotV) * brdfContext.NdotV)))) + 0.0615456;
    float fuzzProb = Efuzz * surface.FuzzWeight;
    specularProb *= 1 - fuzzProb;

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

    const bool isSpecular = Random(randomSeed) < specularProb;

    [branch]
    if (isSpecular)
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

#if defined(RAW_RADIANCE)
    float diffuseAlbedo = 1.0f;
#else
    float3 diffuseAlbedo = surface.DiffuseAlbedo;
#endif

    float3 Fd = diffuseAlbedo * NdotL
        * Diffuse(surface.Roughness, surface.Normal, V, L, brdfContext.NdotV, NdotL, VdotH, VdotL, NdotH)
        * ShadowTerminatorTerm(L, surface.Normal, surface.GeomNormal);

    float3 Fr = F * MonteCarlo::SpecularSampleWeightGGXVNDF(alpha, alpha2, NdotL, brdfContext.NdotV, VdotH, NdotH) * specularPdf;
#if GGX_ENERGY_CONSERVATION
    Fr *= BRDF::GGXEnergyConservationTerm(surface.F0, surface.Roughness, brdfContext.NdotV);
#endif

    // Fuzz
    const float Dfuzz = BRDF::D_Charlie(surface.Roughness, NdotH);
    const float Gfuzz = BRDF::Vis_Neubelt(brdfContext.NdotV, NdotL);
    float3 Ffuzz = surface.FuzzColor * Dfuzz * Gfuzz * NdotL;

    pdf = (1.0f - fuzzProb - specularProb) * diffusePdf + specularProb * specularPdf + fuzzProb * fuzzPdf;

    float pdfRCP = 1.0f / max(pdf, 1e-7f);

    float fuzzMult = lerp(1, 1 - Efuzz, surface.FuzzWeight);
    float fuzzSum = Ffuzz * surface.FuzzWeight;

    brdfWeight.diffuse = (Fd * fuzzMult + fuzzSum) * pdfRCP;
    brdfWeight.specular = (Fr * fuzzMult + fuzzSum) * pdfRCP;

    direction = L;

    return isSpecular;
}
#endif

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

float3 EvaluateDirectRadiance(in Surface surface, in BRDFContext brdfContext, in Instance instance, in Material material, inout uint randomSeed)
{
    float3 radiance = EvalDirectionalLight(surface, brdfContext, Frame.Directional, material, randomSeed) * EvalSkyOcclusion(Frame.Directional.Vector);
    radiance += EvalPointLight(surface, brdfContext, instance.LightData, material, randomSeed);

    return radiance;
}

#endif // SHADING_HLSL