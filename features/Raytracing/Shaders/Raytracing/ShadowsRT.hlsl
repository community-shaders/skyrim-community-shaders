#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/Types/ShadowsFrameData.hlsli"

ConstantBuffer<ShadowsFrameData> Frame  : register(b0);

RWTexture2D<float4> ShadowMask          : register(u0);

Texture2D<float> DepthTexture           : register(t0);
RaytracingAccelerationStructure Scene   : register(t1);

struct Payload
{
    float missed;
};

[shader("raygeneration")]
void RayGeneration()
{
    uint2 idx  = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

	const float depth = DepthTexture[idx];

	const float depthView = ScreenToViewDepth(depth, Frame.CameraData);

    float3 direction = normalize(Frame.Direction.xyz);

    if (depthView < FP_Z || depth >= SKY_Z)
    {
        ShadowMask[idx] = float4(1.0, 0.0, 0.0, 1.0f);
        return;
    }

    float2 uv = (idx + 0.5f) / size;

 	const float3 positionVS = ScreenToViewPosition(uv, depthView, Frame.NDCToView);
	const float3 positionCS = ViewToWorldPosition(positionVS, Frame.ViewInverse);
	const float3 positionWS = positionCS + Frame.Position.xyz;

    RayDesc ray;
    ray.Origin = positionWS + direction * 0.1f;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;

    Payload payload;
    payload.missed = 0.0f;

    TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);

    ShadowMask[idx] = float4(payload.missed, depth, 0.0f, 1.0f);
}

[shader("miss")]
void Miss(inout Payload payload)
{
    payload.missed = 1.0f;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.missed = 0.0f;
}