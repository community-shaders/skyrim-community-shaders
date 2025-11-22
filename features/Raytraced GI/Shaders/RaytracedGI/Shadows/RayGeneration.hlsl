#include "RaytracedGI/Shadows/Payload.hlsli"
#include "RaytracedGI/Includes/Common.hlsli"

cbuffer ShadowsCB: register(b0)
{
    float4 CameraData;
    float2 Size;
    float4 NDCToView;
    float4x4 ViewInverse;
    float3 Position;
    float3 Direction;
    uint Pad[35];
};

RWTexture2D<float4> ShadowMask          : register(u0);

Texture2D<float> DepthTexture           : register(t0);
RaytracingAccelerationStructure Scene   : register(t1);

[shader("raygeneration")]
void main()
{
    uint2 idx  = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
    
	const float depth = DepthTexture[idx];  

	const float depthView = ScreenToViewDepth(depth, CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
        ShadowMask[idx] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }

    float2 uv = (idx + 0.5f) / size;   
    
 	const float3 positionVS = ScreenToViewPosition(uv, depthView, NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, ViewInverse);
	const float3 positionWS = positionCS + Position.xyz;
    
    RayDesc ray;
    ray.Origin = positionWS + Direction * 0.1f;
    ray.Direction = Direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;

    Payload payload;
    payload.missed = 0.0f;    
    
    TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, 0, 0, 0, ray, payload);    

    ShadowMask[idx] = float4(payload.missed, 0.0f, 0.0f, 1.0f);
}
