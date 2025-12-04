#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

#include "Common/Color.hlsli"

[shader("miss")]
void main(inout IndirectPayload payload)
{
    float3 dir = normalize(WorldRayDirection());
    dir.z = max(dir.z, 0.0f);
    
    float r = sqrt(1.0f - dir.z);
    float phi = atan2(dir.y, dir.x);
    
    float2 disk = float2(r * cos(phi), r * sin(phi));
    float2 uv   = disk * 0.5f + 0.5f;   

    payload.color = float4(Color::GammaToLinear(SkyHemisphere.SampleLevel(BaseSampler, uv, 0.0f)) * Frame.Sky, 0.0f);  
    payload.data.SetMissed(true);
}
