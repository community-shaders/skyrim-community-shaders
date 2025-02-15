#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

SamplerState ImageSampler : register(s0);

Texture2D<float4> TexColor : register(t0);

cbuffer VanillaISCB : register(b1)
{
    float3 Cinematic;
    float Width;
    float Height;
};

#define EPSILON 1e-6

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)Width || DTid.y >= (uint)Height)
        return;
    float2 uv = (DTid.xy + 0.5f) / float2(Width, Height);
    float4 color = TexColor.SampleLevel(ImageSampler, uv, 0);

    if (Cinematic.y + Cinematic.z < EPSILON)
    {
        RWTexOut[DTid.xy] = color;
        return;
    }

    float luminance = Color::RGBToLuminance(color.rgb);
    float3 ppColor = color.rgb;
    
    float grayPoint = 0.1f;

    ppColor = Cinematic.y * lerp(luminance, ppColor, Cinematic.x);
    ppColor = clamp(pow(clamp(ppColor, 0.0f, 16.0f), pow(2.0f, Cinematic.z - 1.0f)), 0.0f, 16.0f);
    
    RWTexOut[DTid.xy] = float4(ppColor, color.a);
}