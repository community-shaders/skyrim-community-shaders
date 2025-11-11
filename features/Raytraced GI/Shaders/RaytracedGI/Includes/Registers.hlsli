#ifndef REGISTERS_HLSI
#define REGISTERS_HLSI

#include "RaytracedGI/Includes/Types.hlsli"

ConstantBuffer<FrameData> Frame                 : register(b0);

Texture2D<unorm float3> NormalRoughnessTexture  : register(t0, space0);
Texture2D<float4> GeometryNormalDepthTexture    : register(t1, space0);

RaytracingAccelerationStructure Scene           : register(t2, space0);
StructuredBuffer<Light> Lights                  : register(t3, space0);
StructuredBuffer<Instance> Instances            : register(t4, space0);

StructuredBuffer<Vertex> Vertices[]             : register(t0, space1);
StructuredBuffer<uint3> Triangles[]             : register(t0, space2);
Texture2D DiffuseTextures[]                     : register(t0, space3);

#ifdef SHARC
RWStructuredBuffer<uint64_t>                u_SharcHashEntriesBuffer    : register(u0, space3);
RWStructuredBuffer<SharcAccumulationData>   u_SharcAccumulationBuffer   : register(u1, space3);
RWStructuredBuffer<SharcPackedData>         u_SharcResolvedBuffer       : register(u2, space3);
#endif

SamplerState DiffuseSampler                     : register(s0);

RWTexture2D<float4> DiffuseOutputTexture        : register(u0);
RWTexture2D<float4> SpecularOutputTexture       : register(u1);

#endif