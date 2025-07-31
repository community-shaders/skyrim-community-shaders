#include "Common/SharedData.hlsli"

Texture2D<float3> tExposures : register(t0);
Texture2D<float3> tWeights : register(t1);
RWTexture2D<float3> BlendedRW : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    // Blend the exposures based on the blend weights.
    float3 weights = tExposures[dispatchID.xy];
    float3 exposures = tWeights[dispatchID.xy];
    weights /= dot(weights, 1.0) + 0.0001;
    BlendedRW[dispatchID.xy] = dot(exposures * weights, 1.0);
}