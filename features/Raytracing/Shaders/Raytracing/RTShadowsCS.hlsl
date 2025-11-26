#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"

ConstantBuffer<ShadowsFrameData> Frame  : register(b0);

RWTexture2D<float4> ShadowMask          : register(u0);

Texture2D<float> DepthTexture           : register(t0);
RaytracingAccelerationStructure Scene   : register(t1);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    const float2 size = float2(Frame.Position.w, Frame.Direction.w);
    
    if (any(id > size))
        return;
    
    const float depth = DepthTexture[id];  
    
	const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
        ShadowMask[id] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }
    
    float2 uv = (id + 0.5f) / size;    
    
 	const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
	const float3 positionWS = positionCS + Frame.Position.xyz;
    
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
   
    float3 direction = normalize(Frame.Direction.xyz);
    
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
    
    q.Proceed();
    
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        ShadowMask[id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        ShadowMask[id] = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }    
}