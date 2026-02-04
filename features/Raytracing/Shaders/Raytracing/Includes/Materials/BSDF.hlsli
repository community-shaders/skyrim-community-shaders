// Based on Falcor's BSDF implementation

#ifndef __BSDF_HLSLI__
#define __BSDF_HLSLI__

#include "Common/BRDF.hlsli"
#include "Raytracing/Includes/MathHelpers.hlsli"
#include "Raytracing/Includes/Surface.hlsli"

#include "Raytracing/Includes/Materials/Fresnel.hlsli"
#include "Raytracing/Includes/Materials/LobeType.hlsli"
#include "Raytracing/Includes/Materials/Microfacet.hlsli"

#include "Raytracing/Includes/Materials/HairChiangBSDF.hlsli"

// Minimum cos(theta) for the incident and outgoing vectors.
// Some BSDF functions are not robust for cos(theta) == 0.0,
// so using a small epsilon for consistency.
static const float kMinCosTheta = 1e-6f;

// We clamp the GGX width parameter to avoid numerical instability.
// In some computations, we can avoid clamps etc. if 1.0 - alpha^2 != 1.0, so the epsilon should be 1.72666361e-4 or larger in fp32.
// The the value below is sufficient to avoid visible artifacts.
static const float kMinGGXAlpha = 0.0064f;

static const uint cMaxDeltaLobes = 3;            // 3 should be enough (reflection, transmission, clearcoat reflection?) - there's a bit of a register use cost allowing for more than needed
// This represents delta lobe properties with respect to the surface Wi and surface properties (material settings, texture, normal map, etc.)
struct DeltaLobe
{
    float3  thp;                // how much light goes through the lobe with respect to the surface Wi and this->Wo; will be 0.xxx if probability == 0
    float   probability;        // chance this lobe is sampled with current BSDF importance sampling; will be 0 if disabled; 
    float3  dir;                // refracted or reflected direction in world space when returned from StandardBSDF (tangent space when returned from FalcorBSDF); will be 0.xxx if probability == 0; this is where the ray "will go" in unidirectional path tracing
    int     transmission;       // 1 when transmission lobe, 0 when reflection; even though it can be inferred from Wo, this avoids testing Wo vs triangle normal and potential precision issues

    static DeltaLobe make() { DeltaLobe ret; ret.thp = 0.xxx; ret.dir = 0.xxx; ret.transmission = false; ret.probability = 0; return ret; }
};

/** Describes a BSDF sample.
*/
struct BSDFSample
{
    float3  wo;             ///< Sampled direction in world space (normalized).
    float   pdf;            ///< pdf with respect to solid angle for the sampled direction (wo).
    float3  weight;         ///< Sample weight f(wi, wo) * dot(wo, n) / pdf(wo).
    uint    lobe;           ///< Sampled lobe. This is a combination of LobeType flags (see LobeType.hlsli).
    float   lobeP;          ///< Probability that this lobe sample was picked (including each split between reflection/refraction).

    bool isLobe(LobeType type)
    {
        return (lobe & ((uint)type)) != 0;
    }

    // If delta lobe, returns an unique 2-bit delta lobe identifier (0...3); if not delta lobe returns 0xFFFFFFFF
    // NOTE: this ID must match delta lobe index used in IBSDF::evalDeltaLobes
    uint getDeltaLobeIndex()
    { 
        if ((lobe & (uint)LobeType::Delta) == 0u)
            return 0xFFFFFFFF;
        return (lobe & (uint)LobeType::Transmission) == 0u;    // if transmission return 0, if reflection return 1; TODO: when clearcoat gets added, use 2 for clearcoat reflection
    }
};

// Helper functions for BSDFs
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

struct DiffuseReflection
{
    float3 albedo;
    float roughness;

    float3 Eval(const float3 wi, const float3 wo)
    {
        if (min(wi.z, wo.z) <= kMinCosTheta)
            return float3(0.0f, 0.0f, 0.0f);

        return EvalWeight(wo, wi) * wo.z * K_1_PI;
    }

    bool SampleBSDF(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, float4 preGeneratedSample)
    {
        wo = sample_cosine_hemisphere_concentric(preGeneratedSample.xy, pdf);
        lobe = (uint)LobeType::DiffuseReflection;

        if (min(wo.z, wi.z) <= kMinCosTheta)
        {
            weight = float3(0.0f, 0.0f, 0.0f);
            lobeP = 0.0f;
            return false;
        }

        weight = EvalWeight(wi, wo);
        lobeP = 1.0f;
        return true;
    }

    float EvalPdf(const float3 wi, const float3 wo)
    {
        if (min(wi.z, wo.z) < kMinCosTheta) return 0.f;

        return K_1_PI * wo.z;
    }

    float3 EvalWeight(float3 wo, float3 wi)
    {
        const float3 N = float3(0.0f, 0.0f, 1.0f);
        const float NdotV = saturate(wo.z);
        const float NdotL = saturate(wi.z);
        const float3 H = normalize(wo + wi);
        const float VdotH = max(saturate(dot(wo, H)), kMinCosTheta);
        const float VdotL = saturate(dot(wo, wi));
        const float NdotH = saturate(H.z);

        return albedo * Diffuse(roughness, N, wo, wi, NdotV, NdotL, VdotH, VdotL, NdotH) * K_PI;
    }
};

struct DiffuseTransmissionLambert
{
    float3 albedo;

    float3 Eval(const float3 wi, const float3 wo)
    {
        if (min(wi.z, -wo.z) < kMinCosTheta)
            return float3(0,0,0);

        return K_1_PI * albedo * -wo.z;
    }

    bool SampleBSDF(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, const float4 preGeneratedSample)
    {
        wo = sample_cosine_hemisphere_concentric(preGeneratedSample.xy, pdf);
        wo.z = -wo.z;
        lobe = (uint)LobeType::DiffuseTransmission;

        if (min(wi.z, -wo.z) < kMinCosTheta)
        {
            weight = float3(0,0,0);
            lobeP = 0.0;
            return false;
        }

        weight = albedo;
        lobeP = 1.0;
        return true;
    }

    float EvalPdf(const float3 wi, const float3 wo)
    {
        if (min(wi.z, -wo.z) < kMinCosTheta) return 0.f;

        return K_1_PI * -wo.z;
    }
};

struct SpecularReflectionMicrofacet // : IBxDF
{
    float3 albedo;      ///< Specular albedo.
    float alpha;        ///< GGX width parameter.
    uint activeLobes;   ///< BSDF lobes to include for sampling and evaluation. See LobeType.hlsli.

    bool hasLobe(LobeType lobe) { return (activeLobes & (uint)lobe) != 0; }

    float3 Eval(const float3 wi, const float3 wo)
    {
        if (min(wi.z, wo.z) < kMinCosTheta) return float3(0,0,0);

        // Handle delta reflection.
        if (alpha == 0.f) return float3(0,0,0);

        if (!hasLobe(LobeType::SpecularReflection)) return float3(0,0,0);

        float3 h = normalize(wi + wo);
        float wiDotH = dot(wi, h);

        float D = evalNdfGGX(alpha, h.z);
        float G = evalMaskingSmithGGXCorrelated(alpha, wi.z, wo.z);
        float3 F = evalFresnelSchlick(albedo, 1.f, wiDotH);
        return F * D * G * 0.25f / wi.z;
    }

    bool SampleBSDF(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, const float4 preGeneratedSample)
    {
        wo = float3(0,0,0);
        weight = float3(0,0,0);
        pdf = 0.f;
        lobe = (uint)LobeType::SpecularReflection;
        lobeP = 1.0;

        if (wi.z < kMinCosTheta) return false;

        // Handle delta reflection.
        if (alpha == 0.f)
        {
            if (!hasLobe(LobeType::DeltaReflection)) return false;

            wo = float3(-wi.x, -wi.y, wi.z);
            pdf = 0.f;
            weight = evalFresnelSchlick(albedo, 1.f, wi.z);
            lobe = (uint)LobeType::DeltaReflection;
            return true;
        }

        if (!hasLobe(LobeType::SpecularReflection)) return false;

        // SampleBSDF the GGX distribution to find a microfacet normal (half vector).
        float3 h = sampleGGX_VNDF(alpha, wi, preGeneratedSample.xy);    // pdf = G1(wi) * D(h) * max(0,dot(wi,h)) / wi.z
        
        float wiDotH = dot(wi, h);
        wo = 2.f * wiDotH * h - wi;
        if (wo.z < kMinCosTheta) return false;

        pdf = EvalPdf(wi, wo); // We used to have pdf returned as part of the sampleGGX_XXX functions but this made it easier to add bugs when changing due to code duplication in refraction cases
        weight = Eval(wi, wo) / pdf;
        lobe = (uint)LobeType::SpecularReflection;
        return true;
    }

    float EvalPdf(const float3 wi, const float3 wo)
    {
        if (min(wi.z, wo.z) < kMinCosTheta) return 0.f;

        // Handle delta reflection.
        if (alpha == 0.f) return 0.f;

        if (!hasLobe(LobeType::SpecularReflection)) return 0.f;

        float3 h = normalize(wi + wo);
        float pdf = evalPdfGGX_VNDF(alpha, wi, h);

        return pdf;
    }
};

struct SpecularReflectionTransmissionMicrofacet
{
    float3 transmissionAlbedo;  ///< Transmission albedo.
    float alpha;                ///< GGX width parameter.
    float eta;                  ///< Relative index of refraction (etaI / etaT).
    uint activeLobes;           ///< BSDF lobes to include for sampling and evaluation. See LobeType.hlsli.
    bool isThinSurface;         ///< Hack refraction (but not reflection) eta to 1

    bool hasLobe(LobeType lobe) { return (activeLobes & (uint)lobe) != 0; }

    float3 Eval(const float3 wi, const float3 wo)
    {
        if (min(wi.z, abs(wo.z)) < kMinCosTheta) return float3(0,0,0);

        // Handle delta transmission.
        if (alpha == 0.f) return float3(0,0,0);

        const bool hasReflection = hasLobe(LobeType::SpecularReflection);
        const bool hasTransmission = hasLobe(LobeType::SpecularTransmission);
        const bool isReflection = wo.z > 0.f;
        if ((isReflection && !hasReflection) || (!isReflection && !hasTransmission)) return float3(0,0,0);

        // hack refraction for isThinSurface as the flag means we've entered and left the really thin volume
        float actualEta = (isThinSurface && !isReflection)?(1.0f):(eta);

        // Compute half-vector and make sure it's in the upper hemisphere.
        float3 h = normalize(wo + wi * (isReflection ? 1.f : actualEta));
        h *= float(sign(h.z));

        float wiDotH = dot(wi, h);
        float woDotH = dot(wo, h);

        float D = evalNdfGGX(alpha, h.z);
        float G = evalMaskingSmithGGXCorrelated(alpha, wi.z, abs(wo.z));
        float F = evalFresnelDielectric(actualEta, wiDotH);

        if (isReflection)
        {
            return F * D * G * 0.25f / wi.z;
        }
        else
        {
            float sqrtDenom = woDotH + actualEta * wiDotH;
            float t = actualEta * actualEta * wiDotH * woDotH / (wi.z * sqrtDenom * sqrtDenom);
            return transmissionAlbedo * (1.f - F) * D * G * abs(t);
        }
    }

    bool SampleBSDF(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, const float4 preGeneratedSample)
    {
        wo = float3(0,0,0);
        weight = float3(0,0,0);
        pdf = 0.f;
        lobe = (uint)LobeType::SpecularReflection;
        lobeP = 1;

        if (wi.z < kMinCosTheta) return false;

        float lobeSample = preGeneratedSample.z;

        // Handle delta reflection/transmission.
        [branch]
        if (alpha == 0.f)
        {
            const bool hasReflection = hasLobe(LobeType::DeltaReflection);
            const bool hasTransmission = hasLobe(LobeType::DeltaTransmission);
            if (!(hasReflection || hasTransmission)) return false;

            float cosThetaT;
            float F = evalFresnelDielectric(eta, wi.z, cosThetaT);
            // TODO: adjust F for thin surface hack

            bool isReflection = hasReflection;
            if (hasReflection && hasTransmission)
            {
                isReflection = lobeSample < F;
                lobeP = (isReflection)?(F):(1-F);
            }
            else if (hasTransmission && F == 1.f)
            {
                return false;
            }

            // hack refraction for isThinSurface as the flag means we've entered and left the really thin volume
            float actualEta = eta;
            if (isThinSurface && !isReflection)
            {
                actualEta = 1.0;
                F = evalFresnelDielectric(actualEta, wi.z, cosThetaT);
            }

            pdf = 0.f;
            weight = isReflection ? float3(1,1,1) : transmissionAlbedo;
            if (!(hasReflection && hasTransmission)) weight *= float3( (isReflection ? F : 1.f - F).xxx );
            wo = isReflection ? float3(-wi.x, -wi.y, wi.z) : float3(-wi.x * actualEta, -wi.y * actualEta, -cosThetaT);
            lobe = isReflection ? (uint)LobeType::DeltaReflection : (uint)LobeType::DeltaTransmission;

            if (abs(wo.z) < kMinCosTheta || (wo.z > 0.f != isReflection)) return false;

            return true;
        }

        const bool hasReflection = hasLobe(LobeType::SpecularReflection);
        const bool hasTransmission = hasLobe(LobeType::SpecularTransmission);
        if (!(hasReflection || hasTransmission)) return false;

        float3 h = sampleGGX_VNDF(alpha, wi, preGeneratedSample.xy);    // pdf = G1(wi) * D(h) * max(0,dot(wi,h)) / wi.z

        // Reflect/refract the incident direction to find the outgoing direction.
        float wiDotH = dot(wi, h);

        float cosThetaT;
        float F = evalFresnelDielectric(eta, wiDotH, cosThetaT);

        bool isReflection = hasReflection;
        if (hasReflection && hasTransmission)
        {
            isReflection = lobeSample < F;
        }
        else if (hasTransmission && F == 1.f)
        {
            return false;
        }

        // hack refraction for isThinSurface as the flag means we've entered and left the really thin volume
        float actualEta = eta;
        if (isThinSurface && !isReflection)
        {
            actualEta = 1.0;
            F = evalFresnelDielectric(actualEta, wi.z, cosThetaT);
        }

        wo = isReflection ?
            (2.f * wiDotH * h - wi) :
            ((actualEta * wiDotH - cosThetaT) * h - actualEta * wi);

        if (abs(wo.z) < kMinCosTheta || (wo.z > 0.f != isReflection)) return false;

        float woDotH = dot(wo, h);

        lobe = isReflection ? (uint)LobeType::SpecularReflection : (uint)LobeType::SpecularTransmission;

        pdf = EvalPdf(wi, wo);  // <- this will have the correct Jacobian applied (for correct refraction pdf); We used to have pdf returned as part of the sampleGGX_XXX functions but this made it easier to add bugs when changing due to code duplication in refraction cases
        weight = pdf > 0.f ? Eval(wi, wo) / pdf : float3(0, 0, 0);
        return true;
    }

    float EvalPdf(const float3 wi, const float3 wo)
    {
        if (min(wi.z, abs(wo.z)) < kMinCosTheta) return 0.f;

        // Handle delta reflection/transmission.
        if (alpha == 0.f) return 0.f;

        bool isReflection = wo.z > 0.f;
        const bool hasReflection = hasLobe(LobeType::SpecularReflection);
        const bool hasTransmission = hasLobe(LobeType::SpecularTransmission);
        if ((isReflection && !hasReflection) || (!isReflection && !hasTransmission)) return 0.f;

        // hack refraction for isThinSurface as the flag means we've entered and left the really thin volume
        float actualEta = (isThinSurface && !isReflection)?(1.0f):(eta);

        // Compute half-vector and make sure it's in the upper hemisphere.
        float3 h = normalize(wo + wi * (isReflection ? 1.f : actualEta));
        h *= float(sign(h.z));

        float wiDotH = dot(wi, h);
        float woDotH = dot(wo, h);

        float F = evalFresnelDielectric(actualEta, wiDotH);

        float pdf = evalPdfGGX_VNDF(alpha, wi, h);

        if (isReflection)
        {   // Jacobian of the reflection operator.
            if (woDotH <= 0.f) return 0.f;
            pdf *= wiDotH / woDotH; 
        }
        else
        {   // Jacobian of the refraction operator.
            if (woDotH > 0.f) return 0.f;
            pdf *= wiDotH * 4.0f;
            float sqrtDenom = woDotH + actualEta * wiDotH;
            float denom = sqrtDenom * sqrtDenom;
            pdf *= abs(woDotH) / denom;
        }

        if (hasReflection && hasTransmission)
        {
            pdf *= isReflection ? F : 1.f - F;
        }

        return clamp(pdf, 0, FLT_MAX);
    }
};

struct DefaultBSDF
{
    DiffuseReflection diffuseReflection;
    DiffuseTransmissionLambert diffuseTransmission;
    SpecularReflectionMicrofacet specularReflection;
    SpecularReflectionTransmissionMicrofacet specularReflectionTransmission;

    float diffTrans;                        ///< Mix between diffuse BRDF and diffuse BTDF.
    float specTrans;                        ///< Mix between dielectric BRDF and specular BSDF.

    float pDiffuseReflection;               ///< Probability for sampling the diffuse BRDF.
    float pDiffuseTransmission;             ///< Probability for sampling the diffuse BTDF.
    float pSpecularReflection;              ///< Probability for sampling the specular BRDF.
    float pSpecularReflectionTransmission;  ///< Probability for sampling the specular BSDF.

    void __init(float3 N, float3 V, Surface surface, bool isEnter = true)
    {
        bool isThinSurface = false; // Not used currently

        float3 transmissionAlbedo = surface.TransmissionColor;
        float surfaceRoughness = saturate(surface.Roughness);

        diffuseReflection.albedo = surface.DiffuseAlbedo;
        diffuseReflection.roughness = surfaceRoughness;
        diffuseTransmission.albedo = transmissionAlbedo;

        float alpha = surfaceRoughness * surfaceRoughness;
        if (alpha < kMinGGXAlpha) alpha = 0.f;

        uint activeLobes = (uint)LobeType::DiffuseReflection | (uint)LobeType::SpecularReflection;
        if (transmissionAlbedo.r > 0.f || transmissionAlbedo.g > 0.f || transmissionAlbedo.b > 0.f)
        {
            activeLobes |= (uint)LobeType::DiffuseTransmission | (uint)LobeType::SpecularTransmission | (uint)LobeType::DeltaTransmission;
        }

        float3 surfaceSpecular = surface.F0;
        float surfaceIoR = surface.IOR;
        float surfaceEta = isEnter ? (1.f / surfaceIoR) : surfaceIoR;

        specularReflection.albedo = surfaceSpecular;
        specularReflection.alpha = alpha;
        specularReflection.activeLobes = activeLobes;

        specularReflectionTransmission.transmissionAlbedo = transmissionAlbedo;
        specularReflectionTransmission.alpha = surfaceEta == 1.f ? 0.f : alpha;
        specularReflectionTransmission.eta = surfaceEta;
        specularReflectionTransmission.activeLobes = activeLobes;
        specularReflectionTransmission.isThinSurface = isThinSurface;

        diffTrans = ((activeLobes & (uint)LobeType::Transmission) && surface.SubsurfaceData.HasSubsurface > 0) ? Luminance(surface.TransmissionColor) : 0.f;
        specTrans = ((activeLobes & (uint)LobeType::Transmission) && surface.SubsurfaceData.HasSubsurface == 0) & (uint)LobeType::Transmission ? 1.f : 0.f;

        float surfaceMetallic = surface.Metallic;
        float metallicBRDF = surfaceMetallic * (1.f - specTrans);
        float dielectricBSDF = (1.f - surfaceMetallic) * (1.f - specTrans);
        float specularBSDF = specTrans;

        float diffuseWeight = Luminance(surface.DiffuseAlbedo);
        float specularWeight = Luminance(evalFresnelSchlick(surfaceSpecular, 1.f, dot(V, N)));

        pDiffuseReflection = (activeLobes & (uint)LobeType::DiffuseReflection) ? diffuseWeight * dielectricBSDF * (1.f - diffTrans) : 0.f;
        pDiffuseTransmission = (activeLobes & (uint)LobeType::DiffuseTransmission) ? diffuseWeight * dielectricBSDF * diffTrans : 0.f;
        pSpecularReflection = (activeLobes & ((uint)LobeType::SpecularReflection | (uint)LobeType::DeltaReflection)) ? specularWeight * (metallicBRDF + dielectricBSDF) : 0.f;
        pSpecularReflectionTransmission = (activeLobes & ((uint)LobeType::SpecularReflection | (uint)LobeType::DeltaReflection | (uint)LobeType::SpecularTransmission | (uint)LobeType::DeltaTransmission)) ? specularBSDF : 0.f;

        float normFactor = pDiffuseReflection + pDiffuseTransmission + pSpecularReflection + pSpecularReflectionTransmission;
        if (normFactor > 0.f)
        {
            normFactor = 1.f / normFactor;
            pDiffuseReflection *= normFactor;
            pDiffuseTransmission *= normFactor;
            pSpecularReflection *= normFactor;
            pSpecularReflectionTransmission *= normFactor;
        }
    }

    static DefaultBSDF make(float3 N, float3 V, Surface surface, bool isEnter = true)
    {
        DefaultBSDF bsdf;
        bsdf.__init(N, V, surface, isEnter);
        return bsdf;
    }

    static uint getLobes(Surface surface)
    {
        float surfaceRoughness = saturate(surface.Roughness);
        float alpha = surfaceRoughness * surfaceRoughness;
        bool isDelta = alpha < kMinGGXAlpha;

        float diffTrans = (any(surface.TransmissionColor > 0.f) && surface.SubsurfaceData.HasSubsurface > 0) ? Luminance(surface.TransmissionColor) : 0.f;
        float specTrans = (any(surface.TransmissionColor > 0.f) && surface.SubsurfaceData.HasSubsurface == 0) ? 1.f : 0.f;

        uint lobes = isDelta ? (uint)LobeType::DeltaReflection : (uint)LobeType::SpecularReflection;
        if (any(surface.DiffuseAlbedo > 0.f) && diffTrans < 1.f)
            lobes |= (uint)LobeType::DiffuseReflection;
        
        if (specTrans > 0.f) lobes |= isDelta ? (uint)LobeType::DeltaTransmission : (uint)LobeType::SpecularTransmission;

        return lobes;
    }

    float4 Eval(const float3 wi, const float3 wo)
    {
        float3 diffuse = 0.f; float3 specular = 0.f;
        if (pDiffuseReflection > 0.f) diffuse += (1.f - specTrans) * (1.f - diffTrans) * diffuseReflection.Eval(wi, wo);    // <- this isn't correct; diffuse has a specular component that should be considered
        if (pDiffuseTransmission > 0.f) diffuse += (1.f - specTrans) * diffTrans * diffuseTransmission.Eval(wi, wo);
        if (pSpecularReflection > 0.f) specular += (1.f - specTrans) * specularReflection.Eval(wi, wo);
        if (pSpecularReflectionTransmission > 0.f) specular += specTrans * (specularReflectionTransmission.Eval(wi, wo));   // <- do we want to consider transmission as specular? this depends entirely on denoiser - should ask RR folks

        return float4(diffuse+specular, Average(specular)); // use average instead of sum to avoid hitting fp16 ceiling early
    }

    bool SampleBSDF(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, const float4 preGeneratedSample)
    {
        wo = float3(0,0,0);
        weight = float3(0,0,0);
        pdf = 0.f;
        lobe = (uint)LobeType::DiffuseReflection;
        lobeP = 0.0;

        bool valid = false;

        float uSelect = preGeneratedSample.z;

        if (uSelect < pDiffuseReflection)
        {
            valid = diffuseReflection.SampleBSDF(wi, wo, pdf, weight, lobe, lobeP, preGeneratedSample);
            weight /= pDiffuseReflection;
            weight *= (1.f - specTrans) * (1.f - diffTrans);
            pdf *= pDiffuseReflection;
            lobeP *= pDiffuseReflection;
            if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.EvalPdf(wi, wo);
            if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.EvalPdf(wi, wo);
        }
        else if (uSelect < pDiffuseReflection + pDiffuseTransmission)
        {
            valid = diffuseTransmission.SampleBSDF(wi, wo, pdf, weight, lobe, lobeP, preGeneratedSample);
            weight /= pDiffuseTransmission;
            weight *= (1.f - specTrans) * diffTrans;
            pdf *= pDiffuseTransmission;
            lobeP *= pDiffuseTransmission;
            if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.EvalPdf(wi, wo);
        }
        else if (uSelect < pDiffuseReflection + pDiffuseTransmission + pSpecularReflection)
        {
            valid = specularReflection.SampleBSDF(wi, wo, pdf, weight, lobe, lobeP, preGeneratedSample);
            weight /= pSpecularReflection;
            weight *= (1.f - specTrans);
            pdf *= pSpecularReflection;
            lobeP *= pSpecularReflection;
            if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.EvalPdf(wi, wo);
            if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.EvalPdf(wi, wo);
        }
        else if (pSpecularReflectionTransmission > 0.f)
        {
            valid = specularReflectionTransmission.SampleBSDF(wi, wo, pdf, weight, lobe, lobeP, preGeneratedSample);
            weight /= pSpecularReflectionTransmission;
            weight *= specTrans;
            pdf *= pSpecularReflectionTransmission;
            lobeP *= pSpecularReflectionTransmission;
            if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.EvalPdf(wi, wo);
            if (pDiffuseTransmission > 0.f) pdf += pDiffuseTransmission * diffuseTransmission.EvalPdf(wi, wo);
            if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.EvalPdf(wi, wo);
        }

        if( !valid || (lobe & (uint)LobeType::Delta) != 0 )
            pdf = 0.0;

        return valid;
    }

    float EvalPdf(const float3 wi, const float3 wo)
    {
        float pdf = 0.f;
        if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.EvalPdf(wi, wo);
        if (pDiffuseTransmission > 0.f) pdf += pDiffuseTransmission * diffuseTransmission.EvalPdf(wi, wo);
        if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.EvalPdf(wi, wo);
        if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.EvalPdf(wi, wo);
        return pdf;
    }

    void EvalDeltaLobes(const float3 wi, out DeltaLobe deltaLobes[cMaxDeltaLobes], out int deltaLobeCount, out float nonDeltaPart)  // wi is in local space
    {
        deltaLobeCount = 2;             // currently - will be 1 more if we add clear coat :)
        for (int i = 0; i < deltaLobeCount; i++)
            deltaLobes[i] = DeltaLobe::make(); // init to zero

            nonDeltaPart = pDiffuseReflection+pDiffuseTransmission;
        if ( specularReflection.alpha > 0 ) // if roughness > 0, lobe is not delta
            nonDeltaPart += pSpecularReflection;
        if ( specularReflectionTransmission.alpha > 0 ) // if roughness > 0, lobe is not delta
            nonDeltaPart += pSpecularReflectionTransmission;

        // no spec reflection or transmission? delta lobes are zero (we can just return, already initialized to 0)!
        if ( (pSpecularReflection+pSpecularReflectionTransmission) == 0 )    
            return;

        // note, deltaReflection here represents both this.specularReflection and this.specularReflectionTransmission's
        DeltaLobe deltaReflection, deltaTransmission;
        deltaReflection = deltaTransmission = DeltaLobe::make(); // init to zero
        deltaReflection.transmission    = false;
        deltaTransmission.transmission  = true;

        deltaReflection.dir  = float3(-wi.x, -wi.y, wi.z);

        if (specularReflection.alpha == 0 && specularReflection.hasLobe(LobeType::DeltaReflection))
        {
            deltaReflection.probability = pSpecularReflection;

            // re-compute correct thp for all channels (using float3 version of evalFresnelSchlick!) but then take out the portion that is handled by specularReflectionTransmission below!
            deltaReflection.thp = (1-pSpecularReflectionTransmission)*evalFresnelSchlick(specularReflection.albedo, 1.f, wi.z);
        }

        // Handle delta reflection/transmission.
        if (specularReflectionTransmission.alpha == 0.f)
        {
            const bool hasReflection = specularReflectionTransmission.hasLobe(LobeType::DeltaReflection);
            const bool hasTransmission = specularReflectionTransmission.hasLobe(LobeType::DeltaTransmission);
            if (hasReflection || hasTransmission)
            {
                float cosThetaT;
                float F = evalFresnelDielectric(specularReflectionTransmission.eta, wi.z, cosThetaT);

                if (hasReflection)
                {
                    float localProbability = pSpecularReflectionTransmission * F;
                    float3 weight = float3(1,1,1) * localProbability;
                    deltaReflection.thp += weight;
                    deltaReflection.probability += localProbability;
                }

                if (hasTransmission)
                {
                    // hack refraction for isThinSurface as the flag means we've entered and left the really thin volume
                    // not sure probability is valid - I think it is
                    float actualEta = specularReflectionTransmission.eta;
                    if (specularReflectionTransmission.isThinSurface)
                    {
                        actualEta = 1.0;
                        F = evalFresnelDielectric(actualEta, wi.z, cosThetaT);
                    }

                    float localProbability = pSpecularReflectionTransmission * (1.0-F);
                    float3 weight = specularReflectionTransmission.transmissionAlbedo * localProbability;
                    deltaTransmission.dir  = float3(-wi.x * actualEta, -wi.y * actualEta, -cosThetaT);
                    deltaTransmission.thp = weight;
                    deltaTransmission.probability = localProbability;
                }

                // 
                // if (abs(wo.z) < kMinCosTheta || (wo.z > 0.f != isReflection)) return false;
            }
        }

        // Lobes are by convention in this order, and the index must match BSDFSample::getDeltaLobeIndex() as well as the UI.
        // When we add clearcoat it goes after deltaReflection and so on.
        deltaLobes[0] = deltaTransmission;
        deltaLobes[1] = deltaReflection;
    }
};

struct StandardBSDF
{
    float3 emission;
    bool isEnter;

    static StandardBSDF make(Surface surface, bool isEnter = true)
    {
        StandardBSDF bsdf;
        bsdf.emission = surface.Emissive;
        bsdf.isEnter = isEnter;
        return bsdf;
    }

    float4 Eval(const BRDFContext brdfContext, const Material material, const Surface surface, const float3 wo)
    {
        float3 wi = brdfContext.ViewDirection;
        float3 N = surface.Normal;

        float3 wiLocal = surface.ToLocal(wi);
        float3 woLocal = surface.ToLocal(wo);

#ifdef HAIR_CHIANG_BSDF
        if (material.Feature == Feature::kHairTint)
        {
            HairChiangBSDF bsdf = HairChiangBSDF::make(wi, surface);
            return bsdf.Eval(wiLocal, woLocal);
        } else
#endif
        {
            DefaultBSDF bsdf = DefaultBSDF::make(N, wi, surface, isEnter);
            return bsdf.Eval(wiLocal, woLocal);
        }
    }

    bool SampleBSDF(const float4 preGeneratedSamples, const BRDFContext brdfContext, const Material material, const Surface surface, out BSDFSample result)
    {
        float3 wi = brdfContext.ViewDirection;
        float3 N = surface.Normal;

        float3 wiLocal = surface.ToLocal(wi);

#ifdef HAIR_CHIANG_BSDF
        if (material.Feature == Feature::kHairTint)
        {
            HairChiangBSDF bsdf = HairChiangBSDF::make(wi, surface);

            float3 woLocal;
            bool valid = bsdf.SampleBSDF(wiLocal, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, preGeneratedSamples);

            result.wo = surface.FromLocal(woLocal);
            return valid;
        } else
#endif
        {
            DefaultBSDF bsdf = DefaultBSDF::make(N, wi, surface, isEnter);

            float3 woLocal;
            bool valid = bsdf.SampleBSDF(wiLocal, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, preGeneratedSamples);

            result.wo = surface.FromLocal(woLocal);
            return valid;
        }
    }

    float EvalPdf(const BRDFContext brdfContext, const Surface surface, const float3 wo)
    {
        float3 wi = brdfContext.ViewDirection;
        float3 N = surface.Normal;

        float3 wiLocal = surface.ToLocal(wi);
        float3 woLocal = surface.ToLocal(wo);

        DefaultBSDF bsdf = DefaultBSDF::make(N, wi, surface, isEnter);
        return bsdf.EvalPdf(wiLocal, woLocal);
    }

    uint GetLobes(const Surface surface)
    {
        return DefaultBSDF::getLobes(surface);
    }

    void EvalDeltaLobes(const BRDFContext brdfContext, const Surface surface, out DeltaLobe deltaLobes[cMaxDeltaLobes], out int deltaLobeCount, out float nonDeltaPart)
    {
        float3 wi = brdfContext.ViewDirection;
        float3 N = surface.Normal;

        float3 wiLocal = surface.ToLocal(wi);

        DefaultBSDF bsdf = DefaultBSDF::make(N, wi, surface, isEnter);
        bsdf.EvalDeltaLobes(wiLocal, deltaLobes, deltaLobeCount, nonDeltaPart);

        for (int i = 0; i < deltaLobeCount; i++)
        {
            deltaLobes[i].dir = surface.FromLocal(deltaLobes[i].dir);
        }
    }
};

#endif // __BSDF_HLSLI__