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

#ifndef __HAIRFARFIELDBCSDF_HLSLI__
#define __HAIRFARFIELDBCSDF_HLSLI__

#include "Raytracing/Includes/Materials/HairMaterial.hlsli"
#include "Raytracing/Includes/Materials/LobeType.hlsli"

#include "Raytracing/Includes/Materials/HairBsdfHelper.hlsli"

#include "Raytracing/Includes/MathHelpers.hlsli"

// tighten the R lobe (or not) with phi - [d'Eon et al. 2014 SIGGRAPH talk]
#define R_TERM_AZIMUTHAL_SQUEEZE max(0.01f, cos(0.5f * phi))

// Essential interface functions invoked in generated material code
// Custom far-field BCSDF eval() [Eugene d'Eon - 2022]
//  R lobe: [d'Eon et al. 2014 - SIGGRAPH talk]
//  TT lobe: [Marschner et al. 2003]
//  TRT lobe: custom 3-Gaussian lobe based on fitting to MC simulation
struct HairFarFieldBCSDF
{
    HairMaterialData hairMaterialData;
    HairInteractionSurface hairInteractionSurface;
    HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf;

    void __init(float3 wi, Surface surface)
    {
        hairMaterialData.baseColor = surface.DiffuseAlbedo;
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
        hairMaterialInteractionBcsdf = CreateHairMaterialInteractionBcsdf(hairMaterialData, 0.f, 0.f, surface.Roughness);
    }

    static HairFarFieldBCSDF make(float3 wi, Surface surface)
    {
        HairFarFieldBCSDF bcsdf;
        bcsdf.__init(wi, surface);
        return bcsdf;
    }

    static uint getLobes(Surface surface)
    {
        uint lobes = (uint)LobeType::DiffuseReflection | (uint)LobeType::DiffuseTransmission;

        return lobes;
    }

    float4 Eval(const float3 wi, const float3 wo) // for hair, wi = light dir, wo = view dir
    {
        const float3 tangentU = hairInteractionSurface.tangent; // tangent of hair

        // determine cylindrical coordinates (theta/phi) [Marschner et al. 2003]
        const float sinThetaI = dot(wi, tangentU);
        const float sinThetaO = dot(wo, tangentU);
        const float thetaI = asin(sinThetaI);
        const float thetaO = asin(sinThetaO);
        const float thetaH = 0.5f * (thetaO + thetaI);
        const float thetaD = 0.5f * (thetaO - thetaI);
        const float3 N = normalize(wi - sinThetaI * tangentU);
        const float3 tpo = normalize(wo - sinThetaO * tangentU);
        const float cosPhi = clamp(dot(N, tpo), -1.0f, 1.0f);
        const float phi = acos(cosPhi);

        // load fiber properties
        const float roughness = hairMaterialInteractionBcsdf.roughness;
        const float ior = hairMaterialInteractionBcsdf.ior;
        const float iorSqr = ior * ior;
        const float f0 = CalculateBaseReflectivity(1.0f, ior);
        const float3 mua = hairMaterialInteractionBcsdf.absorptionCoefficient;

        // Compute R lobe - smooth N term, Gaussian M term
        const float fresCosR = cos(0.5f * acos(clamp(dot(wi, wo), -1.0f, 1.0f))); // [d'Eon et al. 2011 - (12)]
        const float fresnelTermR = evalFresnelSchlick(f0, fresCosR).x;
        const float betaR = sqrt(2.0f) * roughness * R_TERM_AZIMUTHAL_SQUEEZE;
        const float M_R = Gaussian1D(thetaH + hairMaterialInteractionBcsdf.cuticleAngle, betaR * 0.5f);
        const float N_R = fresnelTermR * 0.25f * cos(0.5f * phi);

        // Compute TT lobe - smooth N term, Gaussian M term
        const float betaTT = (sinThetaI < 1.0f) ? 0.25f * roughness * Sqrt0(-((-1.0f + iorSqr) / (-1.0f + sinThetaI * sinThetaI))) : 100000.0f;
        const float M_TT = max((sinThetaI < 1.0f) ? Gaussian1D(thetaH - 0.5f * hairMaterialInteractionBcsdf.cuticleAngle, betaTT) : 0.0f, 0.0f);
        const float sinThetaD = sin(thetaD);
        const float etaPrmInv = cos(thetaD) / Sqrt0(iorSqr - sinThetaD * sinThetaD); // 1.0 / eta_prime
        const float etaPrmInvSqr = etaPrmInv * etaPrmInv;
        // hTT: root of phi(h) for p = 1
        const float hTT = clamp(Sqrt01((0.5f + 0.5f * cosPhi) / (1.0f + etaPrmInvSqr - 2.0f * etaPrmInv * Sqrt01(0.5f - 0.5f * cosPhi))), -1.0f, 1.0f);
        const float TTfresnelDot = cos(thetaD) * cos(asin(hTT));
        const float TT_f = evalFresnelSchlick(f0, TTfresnelDot).x; // [d'Eon et al. 2011 - (14)]
        const float fresnelTermTT = (1.0f - TT_f) * (1.0f - TT_f);
        const float N_TT =
            -1.0f / (2.0f * (-2.0f / Sqrt01(1.0f - hTT * hTT) + (2.0f * etaPrmInv) / Sqrt01(1.0f - etaPrmInvSqr * hTT * hTT)));
        const float cosThetaT = cos(thetaD) / (etaPrmInv * ior);
        const float gammaT = asin(hTT * etaPrmInv);
        const float3 absorptionTT = exp(-mua * 2.0f * cos(gammaT) / cosThetaT); // TODO: absorption with Medulla
        const float TTClamp = phi < 2.001f * acos(Sqrt01(1.0f - etaPrmInvSqr)) ? 0.0f : 1.0f;
        const float3 A_TT = fresnelTermTT * absorptionTT * TTClamp;

        // compute TRT lobe as sum of 3 Gaussians
        const float betaTRT = roughness * (2.0f + pow(abs(thetaI), 1.5f));
        const float M_TRT = Gaussian1D(thetaH - 1.5f * hairMaterialInteractionBcsdf.cuticleAngle, betaTRT * 0.5f);
        const float clampTT = (phi < 2.001f * acos(Sqrt01(1.0f - etaPrmInvSqr))) ? 0.0f : 1.0f;

        const float ti = abs(thetaD);

        float p1, w1, w2, v1, v2;

        if (ti < 0.525f)
        {
            p1 = cos(ti) - 0.733f;
            w1 = (-0.000111282f) * (-0.103125f + ti) * ti + pow(ti, 15.7265f) + 0.00023939f;
            w2 = 0.000322755f * ((ti - pow(1.80972f * ti, 16.7669f)) * tan(ti) + 0.991977f);
            v1 = 0.00597578f - (-0.000428897f * cos((5.41149f * ti)));
            v2 = 0.0181f * tan(cos(2.121f * ti));
        }
        else if (ti < 1.1f)
        {
            p1 = max(0.0f, 0.00493f + 0.579f * ti - 0.775f * ti * ti);
            w1 = 0.00108f - 0.0014f * ti + 0.0003937f * ti * ti;
            w2 = -0.00119f + 0.00219f * ti;
            v1 = 0.0391f - 0.0888f * ti + 0.0581f * ti * ti;
            v2 = 0.384f - 1.14f * ti + 0.942f * ti * ti;
        }
        else
        {
            p1 = 0.0f;
            w1 = 0.0f;
            w2 = 0.000239f + 0.00139f * ti * ti * ti - 0.00053124f * ti * ti * ti * ti * ti;
            v1 = 1.0f;
            v2 = -1.86f + 2.73f * ti - 0.7437f * ti * ti;
        }

        const float TRTwidth1 = roughness / 0.06f * Sqrt0(float(v1));
        const float TRTwidth2 = roughness / 0.06f * Sqrt0(float(v2));
        const float N_TRT = float((200.0f / K_PI) * (w1 * Gaussian1D(phi - float(p1), TRTwidth1) + w1 * Gaussian1D(phi + float(p1), TRTwidth1) +
                            w2 * Gaussian1D(phi, TRTwidth2)));

        // assume h = 0 for absorption
        const float3 absorptionTRT = exp(-mua * 3.75f / cosThetaT);
        const float3 A_TRT = absorptionTRT;

        // eval:
        float3 bsdf = max(0.5f * (M_R * N_R + M_TT * N_TT * A_TT * clampTT + M_TRT * N_TRT * A_TRT) / (cos(thetaD) * cos(thetaD)) * cos(thetaI), 0.0f);

        // pdf is just eval() without the absorption terms applied
        float pdf = 0.5f * (M_R * N_R + M_TT * N_TT * fresnelTermTT * TTClamp + M_TRT * N_TRT) / cos(thetaD) / cos(thetaD) * cos(thetaI);

        return float4(bsdf, pdf);
    }

    bool SampleBSDF(const float3 wo, const float h, out float3 wi, out float pdf, out float3 weight, out uint lobe, out float lobeP, const float lobeRandom, const float4 preGeneratedSample)
    {
        const float2 rand2[2] = { preGeneratedSample.xy, preGeneratedSample.zw };

        const float3 T = hairInteractionSurface.tangent;
        const float3 N = hairInteractionSurface.shadingNormal;
        const float3 B = cross(N, T);

        const float sinThetaO = clamp(dot(T, wo), -1.0f, 1.0f);
        const float cosThetaO = Sqrt01(1.0f - sinThetaO * sinThetaO);
        const float thetaO = asin(sinThetaO);

        const float f0 = CalculateBaseReflectivity(1.0f, hairMaterialInteractionBcsdf.ior);
        const float mua = Luminance(hairMaterialInteractionBcsdf.absorptionCoefficient);
        const float ior = hairMaterialInteractionBcsdf.ior;
        const float roughness = hairMaterialInteractionBcsdf.roughness;

        // sample lobe using specular cone propagation at selected h offset
        // equivalent to assuming thetaI = thetaO
        const float aSpec = cosThetaO / Sqrt0(pow(ior, 2.0f) - pow(sinThetaO, 2.0f));
        const float fSpecR = evalFresnelSchlick(f0, cosThetaO * Sqrt01(1.0 - h * h)).x;
        const float fSpecT = 1.0 - fSpecR;
        const float cosThetaTSpec = cosThetaO / (aSpec * ior);
        const float gammaTSpec = asin(h * aSpec);
        const float absorptionSpec = exp(-2.0f * mua * (1.0f + cos(2.0f * gammaTSpec)) / cosThetaTSpec);

        const float hAlbedoR = fSpecR;
        const float hAlbedoTT = fSpecT * absorptionSpec * fSpecT;
        const float hAlbedoTRT = fSpecT * absorptionSpec * fSpecR * absorptionSpec * fSpecT;
        const float hAlbedoNorm = hAlbedoR + hAlbedoTT + hAlbedoTRT;

        const float wR = hAlbedoR / hAlbedoNorm;
        const float wTT = hAlbedoTT / hAlbedoNorm;
        const float wTRT = hAlbedoTRT / hAlbedoNorm;

        const float weightSum = wR + wTT + wTRT;
        const float pdfLobeR = wR / weightSum;
        const float pdfLobeTT= wTT / weightSum;
        const float pdfLobeTRT = wTRT / weightSum;

        float sampleWeight = 0.0f;
        float lobeWeight = 0.0f;
        if (lobeRandom < pdfLobeR)
        {
            // sample R
            const float phi = PhiR(h);
            const float betaR = sqrt(2.0f) * roughness * R_TERM_AZIMUTHAL_SQUEEZE;
            const float thetaI = -thetaO + RandomGaussian1D(rand2[0].x, rand2[0].y) * betaR;

            wi = cos(phi) * cos(thetaI) * N + sin(phi) * cos(thetaI) * B + sin(thetaI) * T;

            const float fresnelTermR = evalFresnelSchlick(f0, cos(0.5f * acos(dot(wi, wo)))).x;

            sampleWeight = clamp(fresnelTermR / wR, 0.0f, 2.0f);

            lobe = (uint)LobeType::DiffuseReflection;
            lobeP = pdfLobeR;
        }
        else if (lobeRandom < pdfLobeR + pdfLobeTT)
        {
            // sample TT
            const float betaTT = (roughness * Sqrt0(-((-1.0f + pow(ior, 2.0f)) / (-1.0f + pow(sinThetaO, 2.0f))))) / 2.0f;
            const float thetaI = -thetaO + RandomGaussian1D(rand2[0].x, rand2[0].y) * betaTT;
            const float thetaD = 0.5f * (thetaI - thetaO);

            const float a = cos(thetaD) / Sqrt0(pow(ior, 2.0f) - pow(sin(thetaD), 2.0f)); // 1.0 / eta_prime
            const float phi = PhiTT(h, a);

            wi = cos(phi) * cos(thetaI) * N + sin(phi) * cos(thetaI) * B + sin(thetaI) * T;

            const float f = evalFresnelSchlick(f0, cos(thetaD) * cos(asin(h))).x; // [d'Eon et al. 2011 - (14)]

            const float cosThetaT = cos(thetaD) / (a * ior);
            const float gammaT = asin(h * a);
            const float absorption = exp(-mua * (1.0f + cos(2.0f * gammaT)) / cosThetaT);

            sampleWeight = clamp((1.0 - f) * (1.0 - f) * absorption / wTT, 0.0f, 2.0f);

            lobe = (uint)LobeType::DiffuseTransmission;
            lobeP = pdfLobeTT;
        }
        else
        {
            // sample TRT
            const float betaTRT = roughness * (2.0f + pow(abs(thetaO), 1.5f));
            const float thetaI = -thetaO + RandomGaussian1D(rand2[0].x, rand2[0].y) * betaTRT;
            const float thetaD = 0.5f * (thetaI - thetaO);

            const float a = cos(thetaD) / Sqrt0(pow(ior, 2.0f) - pow(sin(thetaD), 2.0f)); // 1.0 / eta_prime
            const float phi = PhiTRT(h, a) + RandomGaussian1D(rand2[1].x, rand2[1].y) * roughness;

            wi = cos(phi) * cos(thetaI) * N + sin(phi) * cos(thetaI) * B + sin(thetaI) * T;

            const float f = evalFresnelSchlick(f0, cos(thetaD) * cos(asin(h))).x; // [d'Eon et al. 2011 - (14)]

            const float cosThetaT = cos(thetaD) / (a * ior);
            const float gammaT = asin(h * a);
            const float absorption = exp(-2.3f * mua * (1.0f + cos(2.0f * gammaT)) / cosThetaT);

            sampleWeight = clamp((1.0f - f) * (1.0f - f) * f * absorption / wTRT, 0.0f, 2.0f);

            lobe = (uint)(LobeType::DiffuseReflection);
            lobeP = pdfLobeTRT;
        }

        float4 evalResult = Eval(wi, wo);
        float3 bsdfValue = evalResult.xyz;
        pdf = evalResult.w;

        if (pdf < 1e-6f)
        {
            weight = float3(0.0f, 0.0f, 0.0f);
            lobe = 0;
            lobeP = 0.0f;
            return false;
        }

        weight = max(bsdfValue * sampleWeight / pdf, 0.0f);
        return true;
    }
};

#endif