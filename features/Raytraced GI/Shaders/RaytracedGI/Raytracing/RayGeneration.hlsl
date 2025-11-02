#include "RaytracedGI/Raytracing/Types.hlsli"
#include "RaytracedGI/Raytracing/Common.hlsli"

ConstantBuffer<FrameData> Frame : register(b0);
RaytracingAccelerationStructure Scene : register(t0);

RWTexture2D<float4> Output : register(u0);

[shader("raygeneration")]
void main()
{
    uint2 idx  = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    float2 uv = ((float2(idx) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    uv.y = -uv.y;

    float3 origin = Frame.Position.xyz;
    
    float4 clip = float4(uv, 1.0f, 1.0f);
    float4 view = mul(Frame.ProjInverse, clip);
    view /= view.w; 

    float3 direction = normalize(mul((float3x3)Frame.ViewInverse, view.xyz));   

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.1f;
    ray.TMax = 1e30;
    
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Frame.FrameCount);
    
    Payload payload;
    payload.data = PayloadData::Create(false, 0, seed);

    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    Output[idx] = float4(payload.color, 1);
}
