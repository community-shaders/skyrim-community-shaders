#ifndef __BRDF_DEPENDENCY_HLSL__
#define __BRDF_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

namespace BRDF
{
    // N = Normal of the macro surface
    // H = Normal of the micro surface (halfway vector between L and V)
    // L = Light direction from surface point to light
    // V = View direction from surface point to camera

    // D = Distribution (Microfacet NDF)
    // F = Fresnel
    // Vis = Visibility (Self-shadowing and masking)
    // Specular BRDF = D * Vis * F

    // Diffuse BRDFs
    float Diffuse_Lambert()
    {
        return 1.0 / Math::PI;
    }

    // [Burley 2012, "Physically-Based Shading at Disney"]
    float3 Diffuse_Burley(float roughness, float NdotV, float NdotL, float VdotH)
    {
        float FD90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
        float FdV = 1.0 + (FD90 - 1.0) * pow(1.0 - NdotV, 5.0);
        float FdL = 1.0 + (FD90 - 1.0) * pow(1.0 - NdotL, 5.0);
        return (1.0 / Math::PI) * (FdV * FdL);
    }

    // [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
    float3 Diffuse_OrenNayar(float roughness, float3 N, float3 V, float3 L, float NdotV, float NdotL)
	{
		float a = roughness * roughness * 0.25;
		float A = 1.0 - 0.5 * (a / (a + 0.33));
		float B = 0.45 * (a / (a + 0.09));

		float gamma = dot(V - N * NdotV, L - N * NdotL) / (sqrt(saturate(1.0 - NdotV * NdotV)) * sqrt(saturate(1.0 - NdotL * NdotL)));

		float2 cos_alpha_beta = NdotV < NdotL ? float2(NdotV, NdotL) : float2(NdotL, NdotV);
		float2 sin_alpha_beta = sqrt(saturate(1.0 - cos_alpha_beta * cos_alpha_beta));
		float C = sin_alpha_beta.x * sin_alpha_beta.y / (1e-6 + cos_alpha_beta.y);

		return (1 / Math::PI) * (A + B * max(0.0, gamma) * C);
	}

    // [Gotanda 2014, "Designing Reflectance Models for New Consoles"]
    float3 Diffuse_Gotanda(float roughness, float NdotV, float NdotL, float VdotL)
	{
		float a = roughness * roughness;
		float a2 = a * a;
		float F0 = 0.04;
		float Cosri = VdotL - NdotV * NdotL;
		float Fr = (1 - (0.542026 * a2 + 0.303573 * a) / (a2 + 1.36053)) * (1 - pow(1 - NdotV, 5 - 4 * a2) / (a2 + 1.36053)) * ((-0.733996 * a2 * a + 1.50912 * a2 - 1.16402 * a) * pow(1 - NdotV, 1 + rcp(39 * a2 * a2 + 1)) + 1);
		float Lm = (max(1 - 2 * a, 0) * (1 - pow(1 - NdotL, 5)) + min(2 * a, 1)) * (1 - 0.5 * a * (NdotL - 1)) * NdotL;
		float Vd = (a2 / ((a2 + 0.09) * (1.31072 + 0.995584 * NdotV))) * (1 - pow(1 - NdotL, (1 - 0.3726732 * NdotV * NdotV) / (0.188566 + 0.38841 * NdotV)));
		float Bp = Cosri < 0 ? 1.4 * NdotV * NdotL * Cosri : Cosri;
		float Lr = (21.0 / 20.0) * (1 - F0) * (Fr * Lm + Vd + Bp);
		return (1 / Math::PI) * Lr;
	}

    // [ Chan 2018, "Material Advances in Call of Duty: WWII" ]
    float3 Diffuse_Chan(float roughness, float NdotV, float NdotL, float VdotH, float NdotH)
	{
		float a = roughness * roughness;
		float a2 = a * a;
		float g = saturate((1.0 / 18.0) * log2(2 * rcp(a2) - 1));

		float F0 = VdotH + pow(1 - VdotH, 5);
		float FdV = 1 - 0.75 * pow(1 - NdotV, 5);
		float FdL = 1 - 0.75 * pow(1 - NdotL, 5);

		float Fd = lerp(F0, FdV * FdL, saturate(2.2 * g - 0.5));

		float Fb = ((34.5 * g - 59) * g + 24.5) * VdotH * exp2(-max(73.2 * g - 21.2, 8.9) * sqrt(NdotH));

		return (1 / Math::PI) * (Fd + Fb);
	}

    // Specular BRDFs
    // [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
    float3 F_Schlick(float3 specularColor, float VdotH)
	{
		float Fc = pow(1 - VdotH, 5);
		return Fc + (1 - Fc) * specularColor;
	}

    float3 F_Schlick(float3 F0, float3 F90, float VdotH)
    {
        float Fc = pow(1 - VdotH, 5);
        return F0 + (F90 - F0) * Fc;
    }

    // [Kutz et al. 2021, "Novel aspects of the Adobe Standard Material" ]
    float3 F_AdobeF82(float3 F0, float3 F82, float VdotH)
    {
        const float Fc = pow(1 - VdotH, 5);
        const float K = 49.0 / 46656.0;
        float3 b = (K - K * F82) * (7776.0 + 9031.0 * F0);
        return saturate(F0 + Fc * ((1 - F0) - b * (VdotH - VdotH * VdotH)));
    }

    // [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
    float D_Beckmann(float roughness, float NdotH)
    {
        float NdotH2 = NdotH * NdotH;
        float a = roughness * roughness;
        float a2 = a * a;
        return exp((NdotH2 - 1.0) / (a2 * NdotH2)) / (Math::PI * a2 * NdotH2 * NdotH2);
    }

    // [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
    float D_GGX(float roughness, float NdotH)
    {
        float NdotH2 = NdotH * NdotH;
        float a = roughness * roughness;
        float a2 = a * a;
        float d = NdotH2 * (a2 - 1.0) + 1.0;
        return (a2 / (Math::PI * d * d));
    }

    // [Burley 2012, "Physically-Based Shading at Disney"]
    float D_AnisoGGX(float alphaX, float alphaY, float NdotH, float XdotH, float YdotH)
    {
        float d = XdotH * XdotH / (alphaX * alphaX) + YdotH * YdotH / (alphaY * alphaY) + NdotH * NdotH;
        return rcp(Math::PI * alphaX * alphaY * d * d);
    }

    // [Estevez et al. 2017, "Production Friendly Microfacet Sheen BRDF"]
    float D_Charlie(float roughness, float NdotH)
	{
		float invAlpha = pow(roughness, -4);
		float cos2h = NdotH * NdotH;
		float sin2h = max(1.0 - cos2h, 1e-5);
		return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / Math::TAU;
	}

    // Smith term for GGX
    // [Smith 1967, "Geometrical shadowing of a random rough surface"]
    float Vis_Smith(float roughness, float NdotV, float NdotL)
    {
        float a = roughness * roughness;
        float a2 = a * a;
        float Vis_SmithV = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
        float Vis_SmithL = NdotL + sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
        return rcp(Vis_SmithV * Vis_SmithL);
    }

    // Appoximation of joint Smith term for GGX
    // [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
    float Vis_SmithJointApprox(float roughness, float NdotV, float NdotL)
    {
        float a = roughness * roughness;
        float Vis_SmithV = NdotL * (NdotV * (1.0 + a) + a);
        float Vis_SmithL = NdotV * (NdotL * (1.0 + a) + a);
        return rcp(Vis_SmithV + Vis_SmithL) * 0.5;
    }

    float Vis_SmithJoint(float roughness, float NdotV, float NdotL)
    {
        float a = roughness * roughness;
        float a2 = a * a;
        float Vis_SmithV = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
        float Vis_SmithL = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
        return rcp(Vis_SmithV + Vis_SmithL) * 0.5;
    }

    float Vis_SmithJointAniso(float alphaX, float alphaY, float NdotL, float NdotV, float XdotL, float YdotL, float XdotV, float YdotV)
    {
        float Vis_SmithV = NdotL * length(float3(alphaX * XdotV, alphaY * YdotV, NdotV));
        float Vis_SmithL = NdotV * length(float3(alphaX * XdotL, alphaY * YdotL, NdotL));
        return rcp(Vis_SmithV + Vis_SmithL) * 0.5;
    }

    // [Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"]
    float Vis_Charlie_L(float x, float r)
    {
        r = saturate(r);
        r = 1.0 - (1.0 - r) * (1.0 - r);
        float a = lerp(25.3245 , 21.5473 , r);
        float b = lerp( 3.32435,  3.82987, r);
        float c = lerp( 0.16801,  0.19823, r);
        float d = lerp(-1.27393, -1.97760, r);
        float e = lerp(-4.85967, -4.32054, r);

        return a * rcp((1 + b * pow(x, c)) + d * x + e);
    }

    float Vis_Charlie(float roughness, float NdotV, float NdotL)
    {
        float visV = NdotV < 0.5 ? exp(Vis_Charlie_L(NdotV, roughness)) : exp(2.0 * Vis_Charlie_L(0.5, roughness) - Vis_Charlie_L(1.0 - NdotV, roughness));
        float visL = NdotL < 0.5 ? exp(Vis_Charlie_L(NdotL, roughness)) : exp(2.0 * Vis_Charlie_L(0.5, roughness) - Vis_Charlie_L(1.0 - NdotL, roughness));
        return rcp(((1.0 + visV + visL) * (4.0 * NdotL * NdotV)));
    }

    // [Neubelt et al. 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"]
    float Vis_Neubelt(float NdotV, float NdotL)
    {
        return rcp(4.0 * (NdotL + NdotV - NdotL * NdotV));
    }

    // [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
	float2 EnvBRDFApproxLazarov(float roughness, float NdotV)
	{
		const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
		const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
		float4 r = roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
		float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
		return AB;
	}
}

#endif  // __BRDF_DEPENDENCY_HLSL__