#ifndef BRDF_HLSL
#define BRDF_HLSL

#include "Common/Math.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

namespace BRDF
{ 
    // The following functions bellow all come from NVidia
    float CalcLuminance(float3 color)
    {
        return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
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
        return float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z));
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
    
    float D_GGX_Alpha(float NoH, float alpha)
    {
        float a = NoH * alpha;
        float k = alpha / (1.0 - NoH * NoH + a * a);
        return k * k * (1.0 / Math::PI);
    }

    float3 F_Schlick(float u, float3 f0)
    {
        float f = pow(1.0 - u, 5.0);
        return f + f0 * (1.0 - f);
    }

    float V_SmithGGXCorrelatedFast(float NoV, float NoL, float a)
    {
        float GGXV = NoL * (NoV * (1.0 - a) + a);
        float GGXL = NoV * (NoL * (1.0 - a) + a);
        return 0.5 / (GGXV + GGXL);
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
    
    bool GGXBRDF(float3x3 TBN, in float3 V, in float3 albedo, in float roughness, in float metalness, in float3 f0, inout uint randomSeed, out float3 direction, out float3 BRDF_over_PDF)
    {
        float3 N = TBN[2];
        float3 T = TBN[0];
        float3 B = TBN[1];
    
        float3 diffuseAlbedo = albedo;
    
        float NoV = saturate(dot(N, V));
    
        bool isSpecularRay = false;
        const bool isDeltaSurface = roughness == 0;
        float specular_PDF;
        float overall_PDF;
    
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

        return isSpecularRay;
    }    
}

#endif  // PBR_HLSL