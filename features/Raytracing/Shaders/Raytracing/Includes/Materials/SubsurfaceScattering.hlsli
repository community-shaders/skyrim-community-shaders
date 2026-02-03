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

#ifndef __SUBSURFACESCATTERING_HLSLI__
#define __SUBSURFACESCATTERING_HLSLI__

#include "Raytracing/Includes/Materials/SubsurfaceMaterial.hlsli"

/*//////////////// bibliography ////////////////////
[1] Christensen, P.H. and Burley, B. Approximate Reflectance Profiles for Efficient Subsurface Scattering. 7.
/////////////////////////////////////////////////*/

// [1], S is a scaling factor based on curve fitting, there are different setups
float3 SSS_S(float3 albedo)
{
#ifdef USE_DIFFUSE_MEAN_FREE_PATH
    const float3 A33 = (albedo - 0.33);
    const float3 A332 = A33 * A33;
    return (3.5 + 100*A332*A332);
#else
    const float3 absa = abs(albedo - 0.8);
    return 1.85 - albedo + 7 * absa * absa * absa;
#endif
}

float4 SampleBurleyProfileMIS(
    in float rand,
    in const float3 mfp,
    in const float3 diffuseAlbedo,
    in const float3 ssAlbedo,
    in const bool enableTransmission)
{
    // Importance Sampling Color Channels
    const float3 albedoNormalized = diffuseAlbedo / max(diffuseAlbedo.r + diffuseAlbedo.g + diffuseAlbedo.b, 1e-7f).rrr;
    const float2 channelCdf = float2(albedoNormalized.x, albedoNormalized.x + albedoNormalized.y);
    uint channel = 0;
    if (rand < channelCdf.x)
    {
        rand = rand / channelCdf.x;
    }
    else
    {
        if (rand < channelCdf.y)
        {
            rand = (rand - channelCdf.x) / albedoNormalized.y;
            channel = 1; // sample from green profile: 2 pi r R(r)
        }
        else
        {
            rand = (rand - channelCdf.y) / albedoNormalized.z;
            channel = 2; // sample from blue profile: 2 pi r R(r)
        }
    }

    const float3 s = SSS_S(diffuseAlbedo);
    const float3 d = max(mfp * s, 1e-7f);

    float r = 0.0f;
    if (rand < 0.25f)
    {
        rand *= 4.0f; // Reuse random var and map to [0, 1]
        r = -log(rand) / d[channel]; // r = -log(rand) * l / s = -log(rand) / mfp * s = -log(rand) / d
    }
    else
    {
        rand = (rand - 0.25f) / 0.75f; // Reuse random var and map to [0, 1]
        r = -3.0f * log(rand) / d[channel];
    }

    const float3 pdf3 = 0.25f * d * (exp(-r * d) + exp(-r * d / 3.0f));
    // only subtract single-scattering if transmission is enabled:
    const float3 pdfSS = enableTransmission ? (0.266f * ssAlbedo * (exp(-5.434f * mfp * r) + exp(-1.811f * mfp * r)) * mfp) : 0.0f;
    return float4((diffuseAlbedo * pdf3 - pdfSS) / dot(albedoNormalized, pdf3).rrr, r);
}

void EvalBurleyDiffusionProfile(
    in const SubsurfaceMaterialData subsurfaceMaterialData,
    in const SubsurfaceInteraction subsurfaceInteraction,
    in const float maxSampleRadius,
    in const bool enableTransmission,
    in const float2 rand2,
    inout SubsurfaceSample sssSample)
{
    const SubsurfaceMaterialCoefficients sssMaterialCoeffcients = ComputeSubsurfaceMaterialCoefficients(subsurfaceMaterialData);

    const float4 burleyProfileMisSample = SampleBurleyProfileMIS(rand2.x,
                                                                sssMaterialCoeffcients.sigma_t,
                                                                sssMaterialCoeffcients.albedo,
                                                                sssMaterialCoeffcients.ssAlbedo,
                                                                enableTransmission);
    const float3 bssrdfWeight = burleyProfileMisSample.xyz; // bssrdf / pdf
    const float r = burleyProfileMisSample.w;

    const float l = sqrt(max(maxSampleRadius * maxSampleRadius - r * r, 1e-7f));
    sssSample.samplePosition = CalculateDiskSamplePosition(rand2.y, r, subsurfaceInteraction.centerPosition, subsurfaceInteraction.tangent, subsurfaceInteraction.biTangent)
                              + subsurfaceInteraction.normal * l;
    sssSample.bssrdfWeight = bssrdfWeight;
}

// [Jensen01] A Practical Model for Subsurface Light Transport
// dLo = S(xi, wi; xo, wo) * dLi * cosTheta
//     = C * R(r) * Ft(xi, wi) * Ft(xo, wo) * dLi(xi, wi) * cos(Ni, Li)
// C = 1/pi
//
// TODO: Figure out how to properly handle the rough surface fresnel terms.
//       They currently don't have a closed form solution for BSSRDF.
float3 EvalBssrdf(
    in const SubsurfaceSample sssSample,
    in const float3 incidentRadiance,
    in const float NoL)
{
    const float3 sampleIrradiance = incidentRadiance * NoL.xxx;
    return K_1_PI * sssSample.bssrdfWeight * sampleIrradiance;
}

#endif // __SUBSURFACESCATTERING_HLSLI__