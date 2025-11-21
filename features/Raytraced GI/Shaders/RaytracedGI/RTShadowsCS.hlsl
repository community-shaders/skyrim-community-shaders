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

//Texture2D<float4> GNMDTexture         : register(t0, space1);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (any(id > Size))
        return;
    
    //const float4 gnmd = GNMDTexture[id];     
	//const unorm float depth = gnmd.w * DEPTH_SCALE;  

    //const  float depth = gnmd.w * DEPTH_SCALE;  
    
    const float depth = DepthTexture[id];  
    
	const float depthView = ScreenToViewDepth(depth, CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
        ShadowMask[id] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }
    
    float2 uv = (id + 0.5f) / Size;    
    
 	const float3 positionVS = ScreenToViewPosition(uv, depthView, NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, ViewInverse);
	const float3 positionWS = positionCS + Position.xyz;
    
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
   
    float3 direction = normalize(Direction);
    
    RayDesc ray;
    ray.Origin = positionWS + direction * 0.1f;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;    
    
    q.TraceRayInline(
        Scene,
        RAY_FLAG_NONE,
        0xFF,
        ray);
    
    while (q.Proceed()) {}
    
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        ShadowMask[id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        ShadowMask[id] = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }    
}