#include "RaytracedGI/Includes/Types.hlsli"

#include "RaytracedGI/Includes/Registers.hlsli"

static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);

#ifdef SPECULAR
typedef SpecularPayload CurrentPayload;
#else
typedef DiffusePayload CurrentPayload;
#endif

[shader("miss")]
void main(inout CurrentPayload payload)
{
    float slope = normalize(WorldRayDirection()).z;
    float t = saturate(slope * 5 + 0.5);
    
    payload.color = lerp(skyBottom, skyTop, t);
#ifdef SPECULAR
    payload.distance = RayTCurrent();
#endif
    payload.data.SetMissed(true);
}
