// https://github.com/NVIDIA-RTX/RTXCR
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

#ifndef __HAIR_MATERIAL_HLSLI__
#define __HAIR_MATERIAL_HLSLI__

#define HairLobeType uint
#define HairLobeType_R             (0)
#define HairLobeType_TT            (1)
#define HairLobeType_TRT           (2)
#define Hair_Max_Scattering_Events (3)

#define HairAbsorptionModel uint
#define HairAbsorptionModel_Color      (0)
#define HairAbsorptionModel_Physics    (1)
#define HairAbsorptionModel_Normalized (2)

#define PI_OVER_EIGHT 0.626657069f // sqrt(pi / 8.0f);

/************************************************
    Hair Surface
************************************************/

struct HairInteractionSurface
{
    float3 incidentRayDirection;
    float3 shadingNormal;
    float3 tangent;
};

HairInteractionSurface CreateHairInteractionSurface(
    const float3 incidentRayDirection,
    const float3 tangentWorld,
    const float3 biTangentWorld,
    const float3 normalWorld)
{
    const float3x3 hairTangentBasis = float3x3(tangentWorld, biTangentWorld, normalWorld); // TBN

    const float3 incidentRayDirectionTangentSpace = mul(hairTangentBasis, incidentRayDirection);
    HairInteractionSurface hairInteractionSurface;
    hairInteractionSurface.incidentRayDirection = incidentRayDirectionTangentSpace;
    hairInteractionSurface.shadingNormal = float3(0.0f, 0.0f, 1.0f);
    hairInteractionSurface.tangent = float3(0.0f, 1.0f, 0.0f);
    return hairInteractionSurface;
}

/************************************************
    Hair Material
************************************************/

struct HairMaterialData
{
    float3 baseColor;
    float  longitudinalRoughness; // beta_m

    float  azimuthalRoughness;    // beta_n
    float  ior;
    float  eta;
    uint   fresnelApproximation;

    uint   absorptionModel;
    float  melanin;
    float  melaninRedness;
    float  cuticleAngleInDegrees; // alpha
};

/************************************************
    Hair Interaction - Chiang BSDF
************************************************/

struct HairMaterialInteraction
{
    float  h;
    float  gammaI;
    float3 absorptionCoefficient;

    float  ior;
    float  eta;
    uint   fresnelApproximation;

    float  logisticDistributionScalar; // s

    float v[Hair_Max_Scattering_Events + 1];

    float sin2kAlpha[Hair_Max_Scattering_Events];
    float cos2kAlpha[Hair_Max_Scattering_Events];
};

// Compute Longitudinal Roughness Variance
void ComputeRoughnessVariance(const float betaM, inout HairMaterialInteraction hairMaterialInteraction)
{
    float tmp = 0.726f * betaM + 0.812f * betaM * betaM + 3.7f * pow(betaM, 20.f);
    hairMaterialInteraction.v[0] = max(tmp * tmp, 1e-7f);
    hairMaterialInteraction.v[1] = 0.25f * hairMaterialInteraction.v[0];
    hairMaterialInteraction.v[2] = 4 * hairMaterialInteraction.v[0];
    [unroll]
    for (uint p = 3; p <= Hair_Max_Scattering_Events; ++p)
    {
        hairMaterialInteraction.v[p] = hairMaterialInteraction.v[2];
    }
}

// Compute azimuthally offset h
float CalculateAzimuthallyDistance(const HairInteractionSurface hairInteractionSurface)
{
    // Project wi to the (B, N) plane
    float3 wiProj = normalize(hairInteractionSurface.incidentRayDirection -
        dot(hairInteractionSurface.incidentRayDirection, hairInteractionSurface.tangent) * hairInteractionSurface.tangent);
    // Calculate the vector that perpendicular with projected wi on (B, N) plane
    float3 wiProjPerpendicular = cross(wiProj, hairInteractionSurface.tangent);
    // h = sin(Gamma) = cos(pi/2 - Gamma) = dot(N, Wi_Proj_Prependicular)
    return dot(hairInteractionSurface.shadingNormal, wiProjPerpendicular);
}

// Mapping from color to absorption coefficient.
float3 AbsorptionCoefficientFromColor(const float3 color, const float betaN)
{
    const float tmp = 5.969f - 0.215f * betaN + 2.532f * betaN * betaN - 10.73f * pow(betaN, 3.0f) + 5.574f * pow(betaN, 4.0f) + 0.245f * pow(betaN, 5.0f);
    const float3 sqrtAbsorptionCoefficient = log(max(color, 1e-4f)) / tmp;
    return sqrtAbsorptionCoefficient * sqrtAbsorptionCoefficient;
}

// Mapping from hair melanin to absorption coefficient
float3 ComputeAbsorptionFromMelanin(float eumelanin, float pheomelanin)
{
    return max(eumelanin * float3(0.506f, 0.841f, 1.653f) + pheomelanin * float3(0.343f, 0.733f, 1.924f), float3(0.0f, 0.0f, 0.0f));
}

float3 AbsorptionCoefficientFromMelanin(const float melanin_concentration, const float melanin_redness)
{
    float melanin_concentration_value = melanin_concentration;
    float melanin_gamma = 2.4f;
    float melanin = melanin_concentration_value * melanin_concentration_value * melanin_gamma;
    float eumelanin = melanin * (1.0f - melanin_redness);
    float pheomelanin = melanin * melanin_redness;
    return ComputeAbsorptionFromMelanin(eumelanin, pheomelanin);
}

float3 AbsorptionCoefficientFromMelaninNormalized(const float melanin, const float melaninRedness)
{
    const float melaninQty = -log(max(1.0f - melanin, 0.0001f));
    const float eumelanin = melaninQty * (1.0f - melaninRedness);
    const float pheomelanin = melaninQty * melaninRedness;
    // Adjusted sigma coefficient for range [0, 1]
    const float3 eumelaninSigmaA = float3(0.506f, 0.841f, 1.653f);
    const float3 pheomelaninSigmaA = float3(0.343f, 0.733f, 1.924f);
    return eumelanin.rrr * eumelaninSigmaA + pheomelanin.rrr * pheomelaninSigmaA;
}

float3 ComputeAbsorptionCoefficient(const HairMaterialData hairMaterialData)
{
    switch (hairMaterialData.absorptionModel)
    {
        case HairAbsorptionModel_Color:
            return AbsorptionCoefficientFromColor(hairMaterialData.baseColor, hairMaterialData.azimuthalRoughness);
        case HairAbsorptionModel_Physics:
            return AbsorptionCoefficientFromMelanin(hairMaterialData.melanin, hairMaterialData.melaninRedness);
        case HairAbsorptionModel_Normalized:
            return AbsorptionCoefficientFromMelaninNormalized(hairMaterialData.melanin, hairMaterialData.melaninRedness);
    }
    return float3(0.0f, 0.0f, 0.0f);
}

// Compute azimuthal logistic scale factor
float ComputelogisticDistributionScalar(const float betaN)
{
    return max(PI_OVER_EIGHT * (0.265f * betaN + 1.194f * betaN * betaN + 5.372f * pow(betaN, 22.0f)), 1e-7f);
}

// Compute the scales that caused by the angle between hair cuticle and hair surface
//    /    /    /  <-- Hair Cuticles
//   /    /    /
//  /____/____/____   <-- Hair Surface
//
void ComputeHairCuticleScales(const float cuticleAngleInDegrees, inout HairMaterialInteraction hairMaterialInteraction)
{
    hairMaterialInteraction.sin2kAlpha[0] = sin(cuticleAngleInDegrees / 180.0f * K_PI);
    hairMaterialInteraction.cos2kAlpha[0] = sqrt(saturate(1.f - hairMaterialInteraction.sin2kAlpha[0] * hairMaterialInteraction.sin2kAlpha[0]));
    [unroll]
    for (uint i = 1; i < 3; i++)
    {
        // sin(2*Theta) = 2 * sin(Theta) * cos(Theta)
        hairMaterialInteraction.sin2kAlpha[i] =
            2 * hairMaterialInteraction.cos2kAlpha[i - 1] * hairMaterialInteraction.sin2kAlpha[i - 1];
        // cos(2*Theta) = (cos(Theta))^2 - (sin(Theta))^2
        hairMaterialInteraction.cos2kAlpha[i] =
            hairMaterialInteraction.cos2kAlpha[i - 1] * hairMaterialInteraction.cos2kAlpha[i - 1] -
            hairMaterialInteraction.sin2kAlpha[i - 1] * hairMaterialInteraction.sin2kAlpha[i - 1];
    }
}

HairMaterialInteraction CreateHairMaterialInteraction(
    const HairMaterialData hairMaterialData,
    const HairInteractionSurface hairInteractionSurface)
{
    HairMaterialInteraction hairMaterialInteraction;
    hairMaterialInteraction.h = CalculateAzimuthallyDistance(hairInteractionSurface);
    hairMaterialInteraction.gammaI = asin(clamp(hairMaterialInteraction.h, -1.0f, 1.0f));
    hairMaterialInteraction.absorptionCoefficient = ComputeAbsorptionCoefficient(hairMaterialData);
    hairMaterialInteraction.fresnelApproximation = hairMaterialData.fresnelApproximation;
    hairMaterialInteraction.ior = hairMaterialData.ior;
    hairMaterialInteraction.eta = hairMaterialData.eta;
    hairMaterialInteraction.logisticDistributionScalar = ComputelogisticDistributionScalar(hairMaterialData.azimuthalRoughness);
    // Compute hairMaterialInteraction.v
    ComputeRoughnessVariance(hairMaterialData.longitudinalRoughness, hairMaterialInteraction);
    // Compute Hair Scales
    ComputeHairCuticleScales(hairMaterialData.cuticleAngleInDegrees, hairMaterialInteraction);
    return hairMaterialInteraction;
}

/************************************************
    Hair Interaction - Separate Chiang BSDF
************************************************/

struct HairMaterialSeparateChiangData
{
    HairMaterialData base;

    float longitudinalRoughnessTT;
    float longitudinalRoughnessTRT;
    float azimuthalRoughnessTT;
    float azimuthalRoughnessTRT;
};

struct HairMaterialSeparateChiangInteraction
{
    float h;
    float gammaI;
    float3 absorptionCoefficient;

    float ior;
    float eta;
    uint fresnelApproximation;

    float logisticDistributionScalar[Hair_Max_Scattering_Events + 1]; // s

    float v[Hair_Max_Scattering_Events + 1];

    float sin2kAlpha[Hair_Max_Scattering_Events];
    float cos2kAlpha[Hair_Max_Scattering_Events];
};

float ComputeRoughnessVarianceSeparateChiang(const float betaM)
{
    const float tmp = 0.726f * betaM + 0.812f * betaM * betaM + 3.7f * pow(betaM, 20.f);
    return max(tmp * tmp, 1e-7f);
}

void ComputeHairCuticleScalesSeparateChiang(const float cuticleAngleInDegrees, inout HairMaterialSeparateChiangInteraction hairMaterialSeparateChiangInteraction)
{
    hairMaterialSeparateChiangInteraction.sin2kAlpha[0] = sin(cuticleAngleInDegrees / 180.0f * K_PI);
    hairMaterialSeparateChiangInteraction.cos2kAlpha[0] = sqrt(saturate(1.f - hairMaterialSeparateChiangInteraction.sin2kAlpha[0] * hairMaterialSeparateChiangInteraction.sin2kAlpha[0]));
    [unroll]
    for (uint i = 1; i < 3; i++)
    {
        // sin(2*Theta) = 2 * sin(Theta) * cos(Theta)
        hairMaterialSeparateChiangInteraction.sin2kAlpha[i] =
            2 * hairMaterialSeparateChiangInteraction.cos2kAlpha[i - 1] * hairMaterialSeparateChiangInteraction.sin2kAlpha[i - 1];
        // cos(2*Theta) = (cos(Theta))^2 - (sin(Theta))^2
        hairMaterialSeparateChiangInteraction.cos2kAlpha[i] =
            hairMaterialSeparateChiangInteraction.cos2kAlpha[i - 1] * hairMaterialSeparateChiangInteraction.cos2kAlpha[i - 1] -
            hairMaterialSeparateChiangInteraction.sin2kAlpha[i - 1] * hairMaterialSeparateChiangInteraction.sin2kAlpha[i - 1];
    }
}

HairMaterialSeparateChiangInteraction CreateHairMaterialSeparateChiangInteraction(
    const HairMaterialSeparateChiangData hairMaterialSeparateChiangData,
    const HairInteractionSurface hairInteractionSurface)
{
    HairMaterialSeparateChiangInteraction hairMaterialSeparateChiangInteraction;
    hairMaterialSeparateChiangInteraction.h = CalculateAzimuthallyDistance(hairInteractionSurface);
    hairMaterialSeparateChiangInteraction.gammaI = asin(clamp(hairMaterialSeparateChiangInteraction.h, -1.0f, 1.0f));
    hairMaterialSeparateChiangInteraction.absorptionCoefficient = ComputeAbsorptionCoefficient(hairMaterialSeparateChiangData.base);
    hairMaterialSeparateChiangInteraction.fresnelApproximation = hairMaterialSeparateChiangData.base.fresnelApproximation;
    hairMaterialSeparateChiangInteraction.ior = hairMaterialSeparateChiangData.base.ior;
    hairMaterialSeparateChiangInteraction.eta = hairMaterialSeparateChiangData.base.eta;
    hairMaterialSeparateChiangInteraction.logisticDistributionScalar[0] = ComputelogisticDistributionScalar(hairMaterialSeparateChiangData.base.azimuthalRoughness);
    hairMaterialSeparateChiangInteraction.logisticDistributionScalar[1] = ComputelogisticDistributionScalar(hairMaterialSeparateChiangData.azimuthalRoughnessTT);
    hairMaterialSeparateChiangInteraction.logisticDistributionScalar[2] = ComputelogisticDistributionScalar(hairMaterialSeparateChiangData.azimuthalRoughnessTRT);
    hairMaterialSeparateChiangInteraction.logisticDistributionScalar[3] = hairMaterialSeparateChiangInteraction.logisticDistributionScalar[2];
    // Compute hairMaterialInteraction.v
    hairMaterialSeparateChiangInteraction.v[0] = ComputeRoughnessVarianceSeparateChiang(hairMaterialSeparateChiangData.base.longitudinalRoughness);
    hairMaterialSeparateChiangInteraction.v[1] = ComputeRoughnessVarianceSeparateChiang(hairMaterialSeparateChiangData.longitudinalRoughnessTT);
    hairMaterialSeparateChiangInteraction.v[2] = ComputeRoughnessVarianceSeparateChiang(hairMaterialSeparateChiangData.longitudinalRoughnessTRT);
    hairMaterialSeparateChiangInteraction.v[3] = hairMaterialSeparateChiangInteraction.v[2];
    // Compute Hair Scales
    ComputeHairCuticleScalesSeparateChiang(hairMaterialSeparateChiangData.base.cuticleAngleInDegrees, hairMaterialSeparateChiangInteraction);
    return hairMaterialSeparateChiangInteraction;
}

/************************************************
    Hair Interaction - Farfield BSDF
************************************************/

struct HairMaterialInteractionBcsdf
{
    float3 diffuseReflectionTint;
    float diffuseReflectionWeight;

    float roughness;
    float3 absorptionCoefficient;

    float ior;
    float cuticleAngle;
};

HairMaterialInteractionBcsdf CreateHairMaterialInteractionBcsdf(
    const HairMaterialData hairMaterialData,
    const float3                 diffuseReflectionTint,
    const float                  diffuseReflectionWeight,
    const float                  roughness)
{
    HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf;
    hairMaterialInteractionBcsdf.diffuseReflectionTint = diffuseReflectionTint;
    hairMaterialInteractionBcsdf.diffuseReflectionWeight = diffuseReflectionWeight;
    hairMaterialInteractionBcsdf.roughness = roughness;
    hairMaterialInteractionBcsdf.absorptionCoefficient = ComputeAbsorptionCoefficient(hairMaterialData);
    hairMaterialInteractionBcsdf.ior = hairMaterialData.ior;
    hairMaterialInteractionBcsdf.cuticleAngle = radians(hairMaterialData.cuticleAngleInDegrees);
    return hairMaterialInteractionBcsdf;
}

#endif // __HAIR_MATERIAL_HLSLI__