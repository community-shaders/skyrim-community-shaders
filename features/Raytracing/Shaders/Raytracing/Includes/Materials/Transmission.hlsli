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

#ifndef __TRANSMISSION_HLSLI__
#define __TRANSMISSION_HLSLI__

#include "Raytracing/Includes/MathHelpers.hlsli"
#include "Raytracing/Includes/Materials/SubsurfaceMaterial.hlsli"

float3 SampleHemisphere(float2 u, out float pdf)
{
	const float a = sqrt(u.x);
	const float b = K_2PI * u.y;

	const float3 result = float3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	pdf = result.z * K_1_PI;

	return result;
}

// Evaluate Lamberian Diffuse BRDF
float3 EvalLambertianBRDF(const float3 N, const float3 L, const float3 diffuseAlbedo)
{
    const float NoL = min(max(1e-5f, dot(N, L)), 1.0f);
	return diffuseAlbedo * (K_1_PI * NoL).xxx;
}

/// Calculates Beer-Lambert attenuation at a specified distance through a medium with a specified attenuation coefficient.
float3 EvalBeerLambertAttenuation(in const float3 attenuationCoefficient, in const float distance)
{
    return exp(-attenuationCoefficient * distance);
}

float3 SampleDirectionHenyeyGreenstein(float2 rndSample, in float g, in float3 wo)
{
    float cosTheta;
    if (abs(g) < 1e-3f)
    {
        cosTheta = 1 - 2 * rndSample.x;
    }
    else
    {
        const float sqrTerm = (1 - g * g) / (1 - g + 2 * g * rndSample.x);
        cosTheta = (1 + g * g - sqrTerm * sqrTerm) / (2 * g);
    }

    // Compute direction for Henyey-Greenstein sample
    const float sinTheta = sqrt(max((float) 0, 1 - cosTheta * cosTheta));
    const float phi = RTXCR_TWO_PI * rndSample.y;
    float3 x, y;
    const float3 z = wo;
    CreateCoordinateSystemFromZ(true, z, x, y);
    const float3 wi = SphericalDirection(sinTheta, cosTheta, phi, x, y, z);
    return wi;
}

float3 CalculateRefractionRay(
    in const SubsurfaceInteraction subsurfaceInteraction,
    in const float2 rand2)
{
    // Note: We are doing cosine lobe importance sampling by default, we don't need the pdf because it will be canceled out with BSDF
    //       In case you are using other refraction sampling methods, you need to write your own function to generate refraction ray and calculate PDF
    float bsdfSamplePdf = 0.0f;
    const float3 sampleDirectionLocal = SampleHemisphere(rand2, bsdfSamplePdf);

    const float3x3 tangentBasis = float3x3(subsurfaceInteraction.tangent, -subsurfaceInteraction.biTangent, -subsurfaceInteraction.normal);
    // Note: The tangentBasis is an orthogonal matrix, so we can just do transpose to get the inverse matrix.
    //        This also avoids the issue that HLSL doesn't have inverse matrix intrinsics.
    const float3x3 tangentToWorld = transpose(tangentBasis);
    const float3 refractedRayDirection = mul(tangentToWorld, sampleDirectionLocal);

    return refractedRayDirection;
}

float3 EvaluateBoundaryTerm(
    in const float3 normal,
    in const float3 vectorToLight,
    in const float3 refractedRayDirection,
    in const float3 backfaceNormal,
    in const float thickness,
    in const SubsurfaceMaterialCoefficients sssMaterialCoeffcients)
{
    const float3 boundaryBsdf = EvalLambertianBRDF(backfaceNormal, vectorToLight, sssMaterialCoeffcients.albedo);
    const float3 frontLambertBsdf = EvalLambertianBRDF(-normal, refractedRayDirection, sssMaterialCoeffcients.albedo);
    const float3 volumetricAttenuation = EvalBeerLambertAttenuation(sssMaterialCoeffcients.sigma_t, thickness);

    return boundaryBsdf * volumetricAttenuation * frontLambertBsdf;
}

float3 EvaluateSingleScattering(
    in const float3 vectorToLight,
    in const float3 scatteringBoundaryNormal,
    in const float totalScatteringDistance,
    in const SubsurfaceMaterialCoefficients sssMaterialCoeffcients)
{
    const float3 scatteringBoundaryBsdf = EvalLambertianBRDF(scatteringBoundaryNormal, vectorToLight, sssMaterialCoeffcients.albedo);
    const float3 volumetricAttenuation = EvalBeerLambertAttenuation(sssMaterialCoeffcients.sigma_t, totalScatteringDistance);
    return sssMaterialCoeffcients.sigma_s * scatteringBoundaryBsdf * volumetricAttenuation;
}

#endif // __TRANSMISSION_HLSLI__