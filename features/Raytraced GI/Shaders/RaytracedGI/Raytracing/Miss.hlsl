#include "RaytracedGI/Raytracing/Types.hlsli"

static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);

[shader("miss")]
void main(inout Payload payload)
{

    float slope = -normalize(WorldRayDirection()).z;
    float t = saturate(slope * 5 + 0.5);
    
    payload.color = lerp(skyBottom, skyTop, t);
    payload.data.SetMissed(true);
}
