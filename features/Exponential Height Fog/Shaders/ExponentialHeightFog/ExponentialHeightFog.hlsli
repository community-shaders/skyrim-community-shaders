#ifndef __EXPONENTIAL_HEIGHT_FOG_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_HLSLI__

#include "Common/SharedData.hlsli"

namespace ExponentialHeightFog
{
    float GetFogFactor(float3 positionWS, float3 cameraWS, out float3 directionalInscattering)
    {
        float fogHeightFalloff = SharedData::exponentialHeightFogSettings.fogHeightFalloff * 0.001f;
        float fogDensity = SharedData::exponentialHeightFogSettings.fogDensity * 0.001f;
        if (fogDensity <= 0.0f)
        {
            return 0.0f;
        }
        float3 viewToPos = positionWS;
        float viewToPosLength = length(viewToPos);
        float viewToPosLengthInv = rcp(viewToPosLength);

        float rayOriginTerms = fogDensity * exp2(-fogHeightFalloff * max(cameraWS.z - SharedData::exponentialHeightFogSettings.fogHeight, 0));
        float rayLength = viewToPosLength;
        float rayDirectionZ = viewToPos.z;

        if (SharedData::exponentialHeightFogSettings.startDistance > 0)
        {
            float excludeIntersectionTime = SharedData::exponentialHeightFogSettings.startDistance * viewToPosLengthInv;
            float cameraToExclusionIntersectionZ = excludeIntersectionTime * viewToPos.z;
            float exclusionIntersectionZ = cameraWS.z + cameraToExclusionIntersectionZ;
            rayLength = (1.0f - excludeIntersectionTime) * viewToPosLength;
            rayDirectionZ = viewToPos.z - cameraToExclusionIntersectionZ;
            float exponent = fogHeightFalloff * max(exclusionIntersectionZ - SharedData::exponentialHeightFogSettings.fogHeight, 0);
            rayOriginTerms = fogDensity * exp2(-exponent);
        }

        float falloff = fogHeightFalloff * rayDirectionZ;
        float lineIntegral = (1.0f - exp2(-falloff)) / falloff;
        float lineIntegralTaylor = 0.69314718056f - 0.24022650695f * falloff;  // log(2) - (0.5 * (log(2)^2)) * falloff
        float exponentialHeightLineIntegralCalc = rayOriginTerms * (abs(falloff) > 0.01f ? lineIntegral : lineIntegralTaylor);
        float exponentialHeightLineIntegral = exponentialHeightLineIntegralCalc * rayLength;

        float expFogFactor = saturate(exp2(-exponentialHeightLineIntegral));

        // Calculate directional light inscattering
        float3 directionalLightInscattering = SharedData::DirLightColor.xyz * pow(saturate(dot(normalize(positionWS), SharedData::DirLightDirection.xyz)), SharedData::exponentialHeightFogSettings.directionalInscatteringExponent);
        float dirExponentialHeightLineIntegral = exponentialHeightLineIntegralCalc * max(rayLength - SharedData::exponentialHeightFogSettings.startDistance, 0);
        float dirExpFogFactor = saturate(exp2(-dirExponentialHeightLineIntegral));
        directionalInscattering = directionalLightInscattering * (1 - dirExpFogFactor) * SharedData::exponentialHeightFogSettings.directionalInscatteringMultiplier;

        return 1 - expFogFactor;
    }
}
#endif