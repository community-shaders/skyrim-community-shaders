#include "Common/SharedData.hlsli"
#include "Common/Math.hlsli"

Texture2D<float3> tDiffuse : register(t0);
RWTexture2D<float3> WeightsRW : register(u0);

cbuffer ExposureFusionCB : register(b0)
{
    float4 exposureFusionParams; // x = exposure, y = shadows, z = highlights, w = sigma^2
}
SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{       
    // Compute the synthetic exposure weights.
    uint2 texDim;
    WeightsRW.GetDimensions(texDim.x, texDim.y);
    float2 vUv = (float2(dispatchID.xy) + 0.5) / float2(texDim);

    float3 diff = tDiffuse.SampleLevel(LinearSampler, vUv, 0) - 0.5;
    float3 weights = exp(-0.5 * diff * diff * exposureFusionParams.w);
    weights /= dot(weights, 1.0) + 0.00001;
    WeightsRW[dispatchID.xy] = weights;
}