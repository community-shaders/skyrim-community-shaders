#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Sharc.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"

#include "Common/Color.hlsli"

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
     
    uint seed = InitRandomSeed(idx, size, Frame.FrameCount);
    
    float4 result = TraceRay(Scene, origin, direction, 0, seed);
    
    OutputTexture[idx] = float4(Color::TrueLinearToGamma(result.rgb), 1.0f);
    
    //ReflectanceTexture[idx] = float4(EnvBRDFApprox2(F0(albedo, metalness), roughness, dot(normalWS, viewWS)), 0.0f);
    SpecularHitDist[idx] = result.a;
}
