#include "Common/SharedData.hlsli"
#include "Common/Math.hlsli"

Texture2D<float3> tDiffuse : register(t0);
RWTexture2D<float3> WeightsRW : register(u0);

cbuffer ExposureFusionCB : register(b0)
{
    float4 exposureFusionParams; // x = exposure, y = shadows, z = highlights, w = sigma^2
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{       
    // Compute the synthetic exposure weights.
    float3 diff = tDiffuse[dispatchID.xy] - 0.5;
    float3 weights = exp(-0.5 * diff * diff * exposureFusionParams.w);
    weights /= dot(weights, 1.0) + 0.00001;
    WeightsRW[dispatchID.xy] = weights;
}