/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// Chiang16 Hair model
// Reference Paper: https://benedikt-bitterli.me/pchfm/
// Reference Article: https://www.pbrt.org/hair.pdf

#ifndef __HAIRCHIANGBSDF_HLSLI__
#define __HAIRCHIANGBSDF_HLSLI__

#include "Raytracing/Includes/Materials/HairMaterial.hlsli"
#include "Raytracing/Includes/Materials/LobeType.hlsli"

#include "Raytracing/Includes/Materials/HairBsdfHelper.hlsli"

#include "Raytracing/Includes/MathHelpers.hlsli"

struct HairChiangBSDF
{
    HairMaterialData hairMaterialData;
    HairInteractionSurface hairInteractionSurface;
    HairMaterialInteraction hairMaterialInteraction;

    void __init(float3 wi, Surface surface)
    {
        hairMaterialData.baseColor = surface.DiffuseAlbedo * surface.DiffuseAlbedo;
        hairMaterialData.longitudinalRoughness = surface.Roughness;
        hairMaterialData.azimuthalRoughness = surface.Roughness;

        hairMaterialData.ior = 1.55f; // Typical value for human hair
        hairMaterialData.eta = 1.0f / 1.55f;

        hairMaterialData.fresnelApproximation = 0; // Dielectric
        hairMaterialData.absorptionModel = HairAbsorptionModel_Color; // We don't have melanin data in skyrim
        hairMaterialData.melanin = 0.3f;
        hairMaterialData.melaninRedness = 0.5f;
        hairMaterialData.cuticleAngleInDegrees = 3.0f;

        hairInteractionSurface = CreateHairInteractionSurface(wi, surface.Tangent, surface.Bitangent, surface.Normal);
        hairMaterialInteraction = CreateHairMaterialInteraction(hairMaterialData, hairInteractionSurface);
    }

    static HairChiangBSDF make(float3 wi, Surface surface)
    {
        HairChiangBSDF bsdf;
        bsdf.__init(wi, surface);
        return bsdf;
    }

    static uint getLobes(Surface surface)
    {
        uint lobes = (uint)LobeType::DiffuseReflection | (uint)LobeType::DiffuseTransmission;

        return lobes;
    }

    float4 Eval(const float3 wi, const float3 wo) // for hair, wi = light dir, wo = view dir
    {
        const float sinThetaO = wo.x;
        const float cosThetaO = Sqrt01(1.0f - sinThetaO * sinThetaO);
        const float phiO = Atan2safe(wo.z, wo.y);

        const float sinThetaI = wi.x;
        const float cosThetaI = Sqrt01(1.0f - sinThetaI * sinThetaI);
        const float phiI = Atan2safe(wi.z, wi.y);

        // Compute refracted ray.
        const float sinThetaT = sinThetaO / hairMaterialInteraction.ior;
        const float cosThetaT = Sqrt01(1.0f - sinThetaT * sinThetaT);

        const float etap = Sqrt0(hairMaterialInteraction.ior * hairMaterialInteraction.ior - sinThetaO * sinThetaO) / cosThetaO;
        const float sinGammaT = hairMaterialInteraction.h / etap;
        const float cosGammaT = Sqrt01(1.0f - sinGammaT * sinGammaT);
        const float gammaT = asin(clamp(sinGammaT, -1.0f, 1.0f));

        // Compute the transmittance T of a single path through the cylinder
        const float tmp = -2.0f * cosGammaT / cosThetaT;
        const float3 T = exp(hairMaterialInteraction.absorptionCoefficient * tmp);

        // Evaluate hair BCSDF for each lobe
        const float phi = phiI - phiO;
        float3 ap[Hair_Max_Scattering_Events + 1];
        AP(hairMaterialInteraction, cosThetaO, T, ap);
        float3 result = 0.0f;

        [unroll]
        for (uint p = 0; p < Hair_Max_Scattering_Events; ++p)
        {
            float sinThetaOp, cosThetaOp;
            if (p == 0)
            {
                sinThetaOp = sinThetaO * hairMaterialInteraction.cos2kAlpha[1] - cosThetaO * hairMaterialInteraction.sin2kAlpha[1];
                cosThetaOp = cosThetaO * hairMaterialInteraction.cos2kAlpha[1] + sinThetaO * hairMaterialInteraction.sin2kAlpha[1];
            }
            else if (p == 1)
            {
                sinThetaOp = sinThetaO * hairMaterialInteraction.cos2kAlpha[0] + cosThetaO * hairMaterialInteraction.sin2kAlpha[0];
                cosThetaOp = cosThetaO * hairMaterialInteraction.cos2kAlpha[0] - sinThetaO * hairMaterialInteraction.sin2kAlpha[0];
            }
            else if (p == 2)
            {
                sinThetaOp = sinThetaO * hairMaterialInteraction.cos2kAlpha[2] + cosThetaO * hairMaterialInteraction.sin2kAlpha[2];
                cosThetaOp = cosThetaO * hairMaterialInteraction.cos2kAlpha[2] - sinThetaO * hairMaterialInteraction.sin2kAlpha[2];
            }
            else
            {
                sinThetaOp = sinThetaO;
                cosThetaOp = cosThetaO;
            }

            cosThetaOp = abs(cosThetaOp);
            result += MP(cosThetaOp, cosThetaI, sinThetaOp, sinThetaI, hairMaterialInteraction.v[p]) *
                    ap[p] *
                    NP(phi, p, hairMaterialInteraction.logisticDistributionScalar, hairMaterialInteraction.gammaI, gammaT);
        }

        // Compute contribution of remaining terms after Hair_Max_Scattering_Events
        result += MP(cosThetaO, cosThetaI, sinThetaO, sinThetaI, hairMaterialInteraction.v[Hair_Max_Scattering_Events]) *
                ap[Hair_Max_Scattering_Events] *
                K_1_2PI;

        // We omit this computation in BSDF, because the cosThetaI_N will be cancelled out when evaluate scattered radiance anyway
        // const float cosThetaI_N = wi.z; // The angle between wi and normal, which is (0, 0, 1) on local space
        // result = abs(cosThetaI_N) > 0.0f ? result / abs(cosThetaI_N) : 0.0f;

        return float4(max(result, 0.0f), Average(result));
    }

    bool SampleBSDF(const float3 wo, out float3 wi, out float pdf, out float3 weight, out uint lobe, out float lobeP, const float4 preGeneratedSample)
    {
        float2 u[2];
        u[0] = preGeneratedSample.xy;
        u[1] = preGeneratedSample.zw;

        lobe = (uint)LobeType::DiffuseReflection;
        lobeP = 1.0f;
        uint lobeType;

        const float sinThetaO = wo.x;
        const float cosThetaO = Sqrt01(1.0f - sinThetaO * sinThetaO);
        const float phiO = Atan2safe(wo.z, wo.y);

        // Determine which term p to sample for hair scattering.
        float apPdf[Hair_Max_Scattering_Events + 1];
        ComputeApPdf(hairMaterialInteraction, cosThetaO, apPdf);

        uint p = 0;
        float vp = hairMaterialInteraction.v[0];
        {
            [unroll]
            for (uint i = 0; i < Hair_Max_Scattering_Events; ++i)
            {
                if (u[0].x >= apPdf[i])
                {
                    u[0].x -= apPdf[i];
                    p = i + 1;
                    vp = hairMaterialInteraction.v[i + 1];
                }
                else
                {
                    break;
                }
            }
        }

        float sinThetaOp = sinThetaO;
        float cosThetaOp = cosThetaO;
        if (p == 0)
        {
            sinThetaOp = sinThetaO * hairMaterialInteraction.cos2kAlpha[1] - cosThetaO * hairMaterialInteraction.sin2kAlpha[1];
            cosThetaOp = cosThetaO * hairMaterialInteraction.cos2kAlpha[1] + sinThetaO * hairMaterialInteraction.sin2kAlpha[1];
            lobeType = HairLobeType_R;
        }
        else if (p == 1)
        {
            sinThetaOp = sinThetaO * hairMaterialInteraction.cos2kAlpha[0] + cosThetaO * hairMaterialInteraction.sin2kAlpha[0];
            cosThetaOp = cosThetaO * hairMaterialInteraction.cos2kAlpha[0] - sinThetaO * hairMaterialInteraction.sin2kAlpha[0];

            lobeType = HairLobeType_TT;
        }
        else if (p == 2)
        {
            sinThetaOp = sinThetaO * hairMaterialInteraction.cos2kAlpha[2] + cosThetaO * hairMaterialInteraction.sin2kAlpha[2];
            cosThetaOp = cosThetaO * hairMaterialInteraction.cos2kAlpha[2] - sinThetaO * hairMaterialInteraction.sin2kAlpha[2];

            lobeType = HairLobeType_TRT;
        }
        else
        {
            lobeType = HairLobeType_TRT;
        }

        u[1].x = max(u[1].x, 1e-5f);
        const float cosTheta = 1.0f + vp * log(u[1].x + (1.0f - u[1].x) * exp(-2.0f / vp));
        const float sinTheta = Sqrt01(1.0f - cosTheta * cosTheta);
        const float cosPhi = cos(u[1].y * K_2PI);
        const float sinThetaI = -cosTheta * sinThetaOp + sinTheta * cosPhi * cosThetaOp;
        const float cosThetaI = Sqrt01(1.0f - sinThetaI * sinThetaI);

        // Sample Np to compute dphi
        const float etap = Sqrt0(hairMaterialInteraction.ior * hairMaterialInteraction.ior - sinThetaO * sinThetaO) / cosThetaO;
        const float sinGammaT = hairMaterialInteraction.h / etap;
        const float gammaT = asin(clamp(sinGammaT, -1.0f, 1.0f));
        float dphi;
        if (p < Hair_Max_Scattering_Events)
        {
            dphi = PhiFunction(p, hairMaterialInteraction.gammaI, gammaT) +
                SampleTrimmedLogistic(u[0].y, hairMaterialInteraction.logisticDistributionScalar, -K_PI, K_PI);
        }
        else
        {
            dphi = u[0].y * K_2PI;
        }

        const float phiI = phiO + dphi;
        wi = float3(sinThetaI, cosThetaI * cos(phiI), cosThetaI * sin(phiI));

        pdf = 0.0f;
        [unroll]
        for (uint i = 0; i < Hair_Max_Scattering_Events; ++i)
        {
            float sinThetaIp, cosThetaIp;
            if (i == 0)
            {
                sinThetaIp = sinThetaI * hairMaterialInteraction.cos2kAlpha[1] - cosThetaI * hairMaterialInteraction.sin2kAlpha[1];
                cosThetaIp = cosThetaI * hairMaterialInteraction.cos2kAlpha[1] + sinThetaI * hairMaterialInteraction.sin2kAlpha[1];
            }
            else if (i == 1)
            {
                sinThetaIp = sinThetaI * hairMaterialInteraction.cos2kAlpha[0] + cosThetaI * hairMaterialInteraction.sin2kAlpha[0];
                cosThetaIp = cosThetaI * hairMaterialInteraction.cos2kAlpha[0] - sinThetaI * hairMaterialInteraction.sin2kAlpha[0];
            }
            else if (i == 2)
            {
                sinThetaIp = sinThetaI * hairMaterialInteraction.cos2kAlpha[2] + cosThetaI * hairMaterialInteraction.sin2kAlpha[2];
                cosThetaIp = cosThetaI * hairMaterialInteraction.cos2kAlpha[2] - sinThetaI * hairMaterialInteraction.sin2kAlpha[2];
            }
            else
            {
                sinThetaIp = sinThetaI;
                cosThetaIp = cosThetaI;
            }

            cosThetaIp = abs(cosThetaIp);
            pdf += MP(cosThetaIp, cosThetaO, sinThetaIp, sinThetaO, hairMaterialInteraction.v[i]) *
                    apPdf[i] *
                    NP(dphi, i, hairMaterialInteraction.logisticDistributionScalar, hairMaterialInteraction.gammaI, gammaT);
        }
        pdf += MP(cosThetaI, cosThetaO, sinThetaI, sinThetaO, hairMaterialInteraction.v[RTXCR_Hair_Max_Scattering_Events]) *
                apPdf[Hair_Max_Scattering_Events] *
                K_1_2PI;

        if (pdf > 1e-3f)
        {
            weight = Eval(wi, wo).xyz / pdf;
            // we treat R as specular, TT as diffuse transmission, TRT as diffuse reflection
            if (lobeType == HairLobeType_TT) {
                lobe = (uint)LobeType::DiffuseTransmission;
            } else {
                lobe = (uint)LobeType::DiffuseReflection;
            }
            lobeP = 1.0f;
            return true;
        }
        else
        {
            weight = 0.0f;
            lobe = 0;
            lobeP = 0.0f;
            return false;
        }
    }
};

#endif // __HAIRCHIANGBSDF_HLSLI__