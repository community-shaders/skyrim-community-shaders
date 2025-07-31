#include "Common/SharedData.hlsli"

Texture2D<float3> tExposures : register(t0);
Texture2D<float3> tWeights : register(t1);
RWTexture2D<float3> BlendedRW : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 texDim;
    BlendedRW.GetDimensions(texDim.x, texDim.y);
    float2 vUv = (float2(dispatchID.xy) + 0.5) / float2(texDim);

    // Blend the exposures based on the blend weights.
    float3 weights = tExposures.SampleLevel(LinearSampler, vUv, 0);
    float3 exposures = tWeights.SampleLevel(LinearSampler, vUv, 0);
    weights /= dot(weights, 1.0) + 0.0001;
    BlendedRW[dispatchID.xy] = dot(exposures * weights, 1.0);
}