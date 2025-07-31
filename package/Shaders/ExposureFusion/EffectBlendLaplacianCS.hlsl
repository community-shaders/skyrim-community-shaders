#include "Common/SharedData.hlsli"

Texture2D<float3> tExposures : register(t0);
Texture2D<float3> tExposuresCoarser : register(t1);
Texture2D<float3> tWeights : register(t2);
Texture2D<float3> tAccumSoFar : register(t3);

RWTexture2D<float3> BlendedRW : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 fineDim;
    BlendedRW.GetDimensions(fineDim.x, fineDim.y);
    
    if (dispatchID.x >= fineDim.x || dispatchID.y >= fineDim.y)
        return;

    float2 vUv = (float2(dispatchID.xy) + 0.5) / float2(fineDim);

    // Blend the Laplacians based on exposure weights.
    float accumSoFar = tAccumSoFar.SampleLevel(LinearSampler, vUv, 0);
    float3 laplacians = tExposures[dispatchID.xy] - tExposuresCoarser.SampleLevel(LinearSampler, vUv, 0);
    float3 weights = tWeights[dispatchID.xy] * abs(laplacians);
    weights /= dot(weights, 1.0) + 0.00001;
    float laplac = dot(laplacians * weights, 1.0);
    BlendedRW[dispatchID.xy] = accumSoFar + laplac;
}