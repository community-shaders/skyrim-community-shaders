#ifndef MONTE_CARLO_HLSL
#define MONTE_CARLO_HLSL

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/Surface.hlsli"

namespace MonteCarlo
{
    struct BRDFWeight
    {
        float3 diffuse;
        float3 specular;
    };
    
    // The following functions bellow all come from NVidia
    float CalcLuminance(float3 color)
    {
        return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
    }

    float2 Hammersley( uint Index, uint NumSamples, uint2 Random )
    {
        float E1 = frac( (float)Index / NumSamples + float( Random.x & 0xffff ) / (1<<16) );
        float E2 = float( reversebits(Index) ^ Random.y ) * 2.3283064365386963e-10;
        return float2( E1, E2 );
    }

    float2 Hammersley16( uint Index, uint NumSamples, uint2 Random )
    {
        float E1 = frac( (float)Index / NumSamples + float( Random.x ) * (1.0 / 65536.0) );
        float E2 = float( ( reversebits(Index) >> 16 ) ^ Random.y ) * (1.0 / 65536.0);
        return float2( E1, E2 );
    }

    // It's got a license :(
    // https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/HelperFunctions.hlsli
    float3 SampleGGX_VNDF(float3 Ve, float alpha, inout uint seed)
    {
        float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

        float lensq = Square(Vh.x) + Square(Vh.y);
        float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
        float3 T2 = cross(Vh, T1);

        float r1 = Random(seed);
        float r2 = Random(seed);

        float r = sqrt(r1);
        float phi = 2.0 * Math::PI * r2;
        float t1 = r * cos(phi);
        float t2 = r * sin(phi);
        float s = 0.5 * (1.0 + Vh.z);
        t2 = (1.0 - s) * sqrt(1.0 - Square(t1)) + s * t2;

        float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - Square(t1) - Square(t2))) * Vh;

        // Tangent space H
        return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
    }

    // Also got a license, but a permissive one
    // https://github.com/NVIDIA-RTX/Donut/blob/main/include/donut/shaders/brdf.hlsli
    float ImportanceSampleGGX_VNDF_PDF(float alpha, float3 N, float3 V, float3 L)
    {
        float3 H = normalize(L + V);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(V, H));

        float D = Square(alpha) / (Math::PI * Square(Square(NoH) * Square(alpha) + (1 - Square(NoH))));
        return (VoH > 0.0) ? D / (4.0 * VoH) : 0.0;
    }

    // Keep this for alpha versions of GGX functions
    float GGX_D(float alphaSquared, float NdotH) {
        float b = ((alphaSquared - 1.0f) * saturate(NdotH * NdotH) + 1.0f);
        b = max(b, 0.001f);
        return alphaSquared / (Math::PI * b * b);
    }

    float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared) {
        return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
    }

    // PDF of sampling a reflection vector L using 'sampleGGXVNDF'.
    // Note that PDF of sampling given microfacet normal is (G1 * D) when vectors are in local space (in the hemisphere around shading normal).
    // Remaining terms (1.0f / (4.0f * NdotV)) are specific for reflection case, and come from multiplying PDF by jacobian of reflection operator
    float SampleGGXVNDFReflectionPdf(float alpha, float alphaSquared, float NdotH, float NdotV, float LdotH) {
        NdotH = max(0.00001f, NdotH);
        NdotV = max(0.00001f, NdotV);
        return (GGX_D(max(0.00001f, alphaSquared), NdotH) * Smith_G1_GGX(alpha, NdotV, alphaSquared, NdotV * NdotV)) / (4.0f * NdotV);
    }

    float SpecularSampleWeightGGXVNDF(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) {
        return Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
    }

    float VisibleGGXPDF_aniso(float3 V, float3 H, float2 Alpha, bool bLimitVDNFToReflection = true)
    {
        float NoV = V.z;
        float NoH = H.z;
        float VoH = dot(V, H);
        float a2 = Alpha.x * Alpha.y;
        float3 Hs = float3(Alpha.y * H.x, Alpha.x * H.y, a2 * NoH);
        float S = dot(Hs, Hs);
        float D = (1.0f / Math::PI) * a2 * pow(a2 / S, 2);
        float LenV = length(float3(V.x * Alpha.x, V.y * Alpha.y, NoV));
        float k = 1.0;
        if (bLimitVDNFToReflection)
        {
            float a = saturate(min(Alpha.x, Alpha.y));
            float s = 1.0f + length(V.xy);
            float ka2 = a * a, s2 = s * s;
            k = (s2 - ka2 * s2) / (s2 + ka2 * V.z * V.z); // Eq. 5
        }
        float Pdf = (2 * D * VoH) / (k * NoV + LenV);
        return Pdf;
    }

    // PDF = G_SmithV * VoH * D / NoV / (4 * VoH)
    // PDF = G_SmithV * D / (4 * NoV)
    float4 ImportanceSampleVisibleGGX(float2 E, float2 Alpha, float3 V, bool bLimitVDNFToReflection = true)
    {
        // stretch
        float3 Vh = normalize(float3(Alpha * V.xy, V.z));

        // "Sampling Visible GGX Normals with Spherical Caps"
        // Jonathan Dupuy & Anis Benyoub - High Performance Graphics 2023
        float Phi = (2 * Math::PI) * E.x;
        float k = 1.0;
        if (bLimitVDNFToReflection)
        {
            // If we know we will be reflecting the view vector around the sampled micronormal, we can
            // tweak the range a bit more to eliminate some of the vectors that will point below the horizon
            float a = saturate(min(Alpha.x, Alpha.y));
            float s = 1.0 + length(V.xy);
            float a2 = a * a, s2 = s * s;
            k = (s2 - a2 * s2) / (s2 + a2 * V.z * V.z);
        }
        float Z = lerp(1.0, -k * Vh.z, E.y);
        float SinTheta = sqrt(saturate(1 - Z * Z));
        float X = SinTheta * cos(Phi);
        float Y = SinTheta * sin(Phi);
        float3 H = float3(X, Y, Z) + Vh;

        // unstretch
        H = normalize(float3(Alpha * H.xy, max(0.0, H.z)));

        return float4(H, VisibleGGXPDF_aniso(V, H, Alpha));
    }

    float Schlick_Fresnel(float F0, float VdotH)
    {
        return F0 + (1 - F0) * pow(max(1 - VdotH, 0), 5);
    }

    float3 Schlick_Fresnel(float3 F0, float VdotH)
    {
        return F0 + (1 - F0) * pow(max(1 - VdotH, 0), 5);
    }

    float G1_Smith(float alpha, float NdotL)
    {
        return 2.0 * NdotL / (NdotL + sqrt(Square(alpha) + (1.0 - Square(alpha)) * Square(NdotL)));
    }

    // Compute GGX lobe Weight and Pdf (without Fresnel term) given a set of vectors in local space (Z up)
    float2 GGXEvalReflection(float3 L, float3 V, float3 H, float2 Alpha, bool bLimitVDNFToReflection = true)
    {
        const float NoL = saturate(L.z);
        const float NoV = saturate(V.z);

        if (NoL > 0 && NoV > 0)
        {
            const float D = BRDF::D_AnisoGGX(Alpha.x, Alpha.y, H.z, H.x, H.y);
            // See implementation in Vis_SmithJointAniso for G2/(4*NoV*NoL)
            // We can simplify a bit further since we need both the weight G2/G1 and the pdf
            const float LenL = length(float3(L.xy * Alpha, NoL));
            const float LenV = length(float3(V.xy * Alpha, NoV));
            float k = 1.0;
            if (bLimitVDNFToReflection)
            {
                float a = saturate(min(Alpha.x, Alpha.y));
                float s = 1.0f + length(V.xy);
                float a2 = a * a, s2 = s * s;
                k = (s2 - a2 * s2) / (s2 + a2 * NoV * NoV); // Eq. 5
            }
            const float Weight = NoL * (LenV + k * NoV) / (NoV * LenL + NoL * LenV);
            const float Pdf = 0.5 * D * rcp(LenV + k * NoV);

            return float2(Weight, Pdf);
        }
        return 0;
    }

    // https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md#421-specular-albedo-generation
    float3 EnvBRDFApprox2(float3 SpecularColor, float Alpha, float NoV)
    {
        NoV = abs(NoV);
        // [Ray Tracing Gems, Chapter 32]
        float4 X;
        X.x = 1.f;
        X.y = NoV;
        X.z = NoV * NoV;
        X.w = NoV * X.z;
        float4 Y;
        Y.x = 1.f;
        Y.y = Alpha;
        Y.z = Alpha * Alpha;
        Y.w = Alpha * Y.z;
        float2x2 M1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f);
        float3x3 M2 = float3x3(1.f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f);
        float2x2 M3 = float2x2(0.0365463f, 3.32707, 9.0632f, -9.04756);
        float3x3 M4 = float3x3(1.f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f);
        float bias = dot(mul(M1, X.xy), Y.xy) * rcp(dot(mul(M2, X.xyw), Y.xyw));
        float scale = dot(mul(M3, X.xy), Y.xy) * rcp(dot(mul(M4, X.xzw), Y.xyw));
        // This is a hack for specular reflectance of 0
        bias *= saturate(SpecularColor.g * 50);
        return mad(SpecularColor, max(0, scale), max(0, bias));
    }

    float D_GGXAlpha(float NoH, float alpha)
    {
        float a = NoH * alpha;
        float k = alpha / (1.0 - NoH * NoH + a * a);
        return k * k * (1.0 / Math::PI);
    }

    float V_SmithGGXCorrelatedFast(float NoV, float NoL, float a)
    {
        float GGXV = NoL * (NoV * (1.0 - a) + a);
        float GGXL = NoV * (NoL * (1.0 - a) + a);
        return 0.5 / (GGXV + GGXL);
    }

    // Calculates probability of selecting BRDF (specular or diffuse) using the approximate Fresnel term
    float GetSpecularBrdfProbability(Surface surface, float3 viewVector, float3 shadingNormal)
    {
        // Evaluate Fresnel term using the shading normal
        // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
        float specularF0 = CalcLuminance(surface.F0);
        float diffuseReflectance = CalcLuminance(surface.DiffuseAlbedo);

        float fresnel = saturate(CalcLuminance(BRDF::F_Schlick(specularF0, BRDF::ShadowedF90(specularF0), max(0.0f, dot(viewVector, shadingNormal)))));

        // Approximate relative contribution of BRDFs using the Fresnel term
        float specular = fresnel;
        float diffuse = diffuseReflectance * (1.0f - fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

        // Return probability of selecting specular BRDF over diffuse BRDF
        float probability = (specular / max(0.0001f, (specular + diffuse)));

        // Clamp probability to avoid undersampling of less prominent BRDF
        return clamp(probability, 0.1f, 0.9f);
    }

    // Helper functions for multiple importance sampling

    // Multiple importance sampling balance heuristic
    // [Veach 1997, "Robust Monte Carlo Methods for Light Transport Simulation"]
    float MISWeightBalanced(float Pdf, float OtherPdf)
    {
        // The straightforward implementation is prone to numerical overflow, divisions by 0
        // and does not work well with +inf inputs.
        // return Pdf / (Pdf + OtherPdf);

        // We want this function to have the following properties:
        //  0 <= w(a,b) <= 1 for all possible positive floats a and b (including 0 and +inf)
        //  w(a, b) + w(b, a) == 1.0

        // The formulation below is much more stable across the range of all possible inputs
        // and guarantees the sum always adds up to 1.0.

        // Evaluate the expression using the ratio of the smaller value to the bigger one for greater
        // numerical stability. The math would also work using the ratio of bigger to smaller value,
        // which would underflow less but would make the weights asymmetric. Underflow to 0 is not a
        // bad property to have in rendering application as it ensures more weights are exactly 0
        // which allows some evaluations to be skipped.
        float X = min(Pdf, OtherPdf) / max(Pdf, OtherPdf); // This ratio is guaranteed to be in [0,1]
        float Y = Pdf == OtherPdf ? 1.0 : X; // Guard against NaNs from 0/0 and Inf/Inf
        float M = rcp(1.0 + Y);
        return Pdf > OtherPdf ? M : 1.0 - M; // This ensures exchanging arguments will produce values that add back up to 1.0 exactly
    }

    // Multiple importance sampling power heuristic of two functions with a power of two.
    // [Veach 1997, "Robust Monte Carlo Methods for Light Transport Simulation"]
    float MISWeightPower(float Pdf, float OtherPdf)
    {
        // Naive code (which can overflow, divide by 0, etc ..)
        // return Pdf * Pdf / (Pdf * Pdf + OtherPdf * OtherPdf);

        // See function above for the explanation of how this works
        float X = min(Pdf, OtherPdf) / max(Pdf, OtherPdf); // This ratio is guaranteed to be in [0,1]
        float Y = Pdf == OtherPdf ? 1.0 : X; // Guard against NaNs from 0/0 and Inf/Inf
        float M = rcp(1.0 + Y * Y);
        return Pdf > OtherPdf ? M : 1.0 - M; // This ensures exchanging arguments will produce values that add back up to 1.0 exactly
    }

    // Takes as input the sample weight and pdf for a certain lobe of a mixed model, together with the probability of picking that lobe
    // This function then updates a running total Weight and Pdf value that represents the overall contribution of the BxDF
    // This function should be called when a BxDF is made up of multiple lobes combined with a sum to correctly account for the probability
    // of sampling directions via all lobes.
    // NOTE: this function also contains special logic to handle cases with infinite pdfs cleanly
    void AddLobeWithMIS(inout float3 Weight, inout float Pdf, float3 LobeWeight, float LobePdf, float LobeProb)
    {
        const float MinLobeProb = 1.1754943508e-38; // smallest normal float
        if (LobeProb > MinLobeProb)
        {
            LobePdf *= LobeProb;
            LobeWeight *= rcp(LobeProb);
            Weight = lerp(Weight, LobeWeight, MISWeightBalanced(LobePdf, Pdf));
            Pdf += LobePdf;
        }
    }

    float3 DiffuseAO(float3 diffuseColor, float ao)
    {
        return Color::MultiBounceAO(diffuseColor, ao);
    }

    float3 SpecularAO(float NdotV, float roughness, float ao, float3 f0)
    {
        float specularAO = Color::SpecularAOLagarde(NdotV, ao, roughness);
        return Color::MultiBounceAO(f0, specularAO);
    }

    // Horizon specular occlusion
    // https://marmosetco.tumblr.com/post/81245981087
    float Horizon(float3 V, float3 N, float3 VN)
    {
        float3 R = reflect(-V, N);
        float horizon = min(1.0 + dot(R, VN), 1.0);

        return horizon * horizon;
    }

    // https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/LightingPasses/BrdfRayTracing.hlsl
    bool GGXBRDF(in Surface surface, in BRDFContext brdfContext, inout uint randomSeed, out float3 direction, out float3 BRDF_over_PDF)
    {
        bool isSpecularRay = false;
        const bool isDeltaSurface = surface.Roughness == 0;
        float specular_PDF;
        float overall_PDF;

        {
            float3 specularDirection;
            float3 specular_BRDF_over_PDF;
            {
                float3 Ve = float3(
                    dot(brdfContext.ViewDirection, surface.Tangent),
                    dot(brdfContext.ViewDirection, surface.Bitangent),
                    dot(brdfContext.ViewDirection, surface.Normal)
                );

                const float alpha = surface.Roughness * surface.Roughness;

                float3 He = SampleGGX_VNDF(Ve, alpha, randomSeed);
                float3 H = isDeltaSurface ? surface.Normal : surface.Mul(He);
                specularDirection = reflect(-brdfContext.ViewDirection, H);

                float HoV = saturate(dot(H, brdfContext.ViewDirection));
                float3 F = Schlick_Fresnel(surface.F0, HoV);
                float G1 = isDeltaSurface ? 1.0 : (brdfContext.NdotV > 0) ? G1_Smith(alpha, brdfContext.NdotV) : 0;
                specular_BRDF_over_PDF = F * G1;
            }

            float3 diffuseDirection;
            float diffuse_BRDF_over_PDF;
            {
                float3 localDirection = SampleCosineHemisphere(randomSeed);
                diffuseDirection = surface.Mul(localDirection);
                diffuse_BRDF_over_PDF = 1.0;
            }

            specular_PDF = saturate(CalcLuminance(specular_BRDF_over_PDF) /
                CalcLuminance(specular_BRDF_over_PDF + diffuse_BRDF_over_PDF * surface.DiffuseAlbedo));

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

        return isSpecularRay;
    }

    // When sampling from discrete CDFs, it can be convenient to re-use the random number by rescaling it
    // This function assumes that RandVal is in the interval: [LowerBound, UpperBound) and returns a value in [0,1)
    float RescaleRandomNumber(float RandVal, float LowerBound, float UpperBound)
    {
        const float OneMinusEpsilon = 0.99999994; // 32-bit float just before 1.0
        return min((RandVal - LowerBound) / (UpperBound - LowerBound), OneMinusEpsilon);
    }
}

#endif  // MONTE_CARLO_HLSL