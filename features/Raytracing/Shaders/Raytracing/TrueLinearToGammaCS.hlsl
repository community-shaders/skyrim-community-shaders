#include "Common/Color.hlsli"

Texture2D<float4> FinalTexture  : register(t0);
RWTexture2D<float4> Main        : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    Main[id] = float4(Color::TrueLinearToGamma(FinalTexture[id].rgb), 1.0f);
}