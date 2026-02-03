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

#ifndef __SUBSURFACEMATERIAL_HLSLI__
#define __SUBSURFACEMATERIAL_HLSLI__

#include "Raytracing/Includes/MathHelpers.hlsli"

#define MAX_SSS_SAMPLE_COUNT 4

#define SSS_METERS_UNIT 0.01428f // Skyrim units to meters

#define SSS_MIN_ALBEDO 0.01f

/************************************************
    Subsurface Material
************************************************/

struct SubsurfaceMaterialData
{
    float3 transmissionColor;
    float  g;

    float3 scatteringColor;
    float  scale;
};

struct SubsurfaceInteraction
{
    float3 centerPosition;

    float3 normal;
    float3 tangent;
    float3 biTangent;
};

struct SubsurfaceSample
{
    float3 samplePosition;
    float3 bssrdfWeight;
};

struct VolumeCoefficients
{
    float3 scattering;
    float3 absorption;
};

struct SubsurfaceMaterialCoefficients
{
    float3 sigma_s;
    float3 sigma_t;
    float3 albedo;
    float3 ssAlbedo;
};

// Helper functions
SubsurfaceMaterialData CreateDefaultSubsurfaceMaterialData()
{
    SubsurfaceMaterialData subsurfaceMaterialData;
    subsurfaceMaterialData.transmissionColor = float3(0.0f, 0.0f, 0.0f);
    subsurfaceMaterialData.scatteringColor = float3(0.0f, 0.0f, 0.0f);
    subsurfaceMaterialData.g = 0.0f;
    subsurfaceMaterialData.scale = 0.0f;
    return subsurfaceMaterialData;
}

SubsurfaceInteraction CreateSubsurfaceInteraction(
    const float3 centerPosition,
    const float3 normal,
    const float3 tangent,
    const float3 biTangent)
{
    SubsurfaceInteraction subsurfaceInteraction;
    subsurfaceInteraction.centerPosition = centerPosition;
    subsurfaceInteraction.normal = normal;
    subsurfaceInteraction.tangent = tangent;
    subsurfaceInteraction.biTangent = biTangent;

    return subsurfaceInteraction;
}

//https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
float3 ComputeTransmissionAlbedo(in const float3 transmissionColor)
{
    return float3(4.09712f, 4.09712f, 4.09712f) +
           (4.20863f * transmissionColor) -
           Sqrt0(9.59217f +
                       41.6808f * transmissionColor +
                       17.7126f * transmissionColor * transmissionColor);
}

VolumeCoefficients ComputeSubsurfaceVolumeCoefficients(in const SubsurfaceMaterialData sssData)
{
    const float3 s = ComputeTransmissionAlbedo(sssData.transmissionColor);
    const float3 alpha = (WhiteColor - s * s) / max(WhiteColor - sssData.g * (s * s), 1e-7f);
    const float scale = SSS_METERS_UNIT * sssData.scale;
    const float3 scatteringRadius = max(scale.rrr * sssData.scatteringColor, 1e-7f);

    VolumeCoefficients subsurfaceVolumeCoefficients;
    subsurfaceVolumeCoefficients.scattering = alpha / scatteringRadius;
    subsurfaceVolumeCoefficients.absorption =
        (WhiteColor / scatteringRadius) - subsurfaceVolumeCoefficients.scattering;

    return subsurfaceVolumeCoefficients;
}

SubsurfaceMaterialCoefficients ComputeSubsurfaceMaterialCoefficients(in const SubsurfaceMaterialData sssData)
{
    VolumeCoefficients volumeCoefficients = ComputeSubsurfaceVolumeCoefficients(sssData);
    const float3 sigma_a = volumeCoefficients.absorption;
    const float3 sigma_s = volumeCoefficients.scattering;
    const float3 sigma_t = max(sigma_a + sigma_s, 1e-7f);

    const float3 mfp = 1.0f.rrr / sigma_t;
    const float3 s = Sqrt0(sigma_a * mfp); // sigma_a / sigma_t

    // custom diffuse albedo prediction based on MC simulation of isotropic scattering, diffuse transmittance on entry
    // and Fresnel reflection back into the volume assuming ior = 1.4 (as if the air outside was denser)
    SubsurfaceMaterialCoefficients subsurfaceMaterialCoefficients;
    subsurfaceMaterialCoefficients.sigma_s = sigma_s;
    subsurfaceMaterialCoefficients.sigma_t = sigma_a + sigma_s;
    subsurfaceMaterialCoefficients.albedo = 0.88f * (1.0f - s) / (1.0f + 1.5535f * s);
    subsurfaceMaterialCoefficients.ssAlbedo = max(SSS_MIN_ALBEDO, sigma_s / sigma_t);

    return subsurfaceMaterialCoefficients;
}

#endif // __SUBSURFACEMATERIAL_HLSLI__