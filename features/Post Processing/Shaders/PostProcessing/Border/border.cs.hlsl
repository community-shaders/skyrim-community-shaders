#include "Common/SharedData.hlsli"

Texture2D <float4> InputTexture : register(t0);
Texture2D <float> DepthTexture : register(t1);

RWTexture2D <float4> OutputTexture : register(u0);

cbuffer BorderCB : register(b1)
{
    float4 BorderColor; // color in xyz, depth threshold in w
    float4 Scale; // xyzw: up, down, left, right
};

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float depth = DepthTexture[DTid.xy];
    float3 borderColor = BorderColor.xyz;
    float depthThreshold = BorderColor.w;
    if (depth > depthThreshold || depthThreshold == 0.0f)
    {
        float2 uv = (DTid.xy + 0.5f) * SharedData::BufferDim.zw;
        if (uv.y < Scale.x || uv.y > (Scale.y) || uv.x < Scale.z || uv.x > (Scale.w))
        {
            OutputTexture[DTid.xy] = float4(borderColor, 1.0);
        }
    }
    else
    {
        OutputTexture[DTid.xy] = InputTexture[DTid.xy];
    }
}