#include "Common/SharedData.hlsli"

Texture2D<float3> InputTexture : register(t0);
RWTexture2D<float3> OutputRW : register(u0);
SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 outputDim;
    OutputRW.GetDimensions(outputDim.x, outputDim.y);
    
    if (dispatchID.x >= outputDim.x || dispatchID.y >= outputDim.y)
        return;
        
    // Downsample by 2x using bilinear sampling
    float2 uv = (float2(dispatchID.xy) + 0.5) / float2(outputDim);
    float3 color = InputTexture.SampleLevel(LinearSampler, uv, 0).rgb;
    
    OutputRW[dispatchID.xy] = color;
}