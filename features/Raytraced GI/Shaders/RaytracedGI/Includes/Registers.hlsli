#ifndef REGISTERS_HLSI
#define REGISTERS_HLSI

#include "RaytracedGI/Includes/Types.hlsli"

ConstantBuffer<FrameData> Frame                 : register(b0);

Texture2D<float4> AlbedoTexture                 : register(t0, space0);
Texture2D<float4> ReflectanceTexture            : register(t1, space0);
Texture2D<snorm float4> NormalRoughnessTexture  : register(t2, space0);
Texture2D<float4> NormalMetalnessDepthTexture   : register(t3, space0);

RaytracingAccelerationStructure Scene           : register(t4, space0);
Texture2D<float3> SkyHemisphere                 : register(t5, space0);
StructuredBuffer<Light> Lights                  : register(t6, space0);
StructuredBuffer<Instance> Instances            : register(t7, space0);

StructuredBuffer<Vertex> Vertices[]             : register(t0, space1);
StructuredBuffer<uint3> Triangles[]             : register(t0, space2);
Texture2D<float4> DiffuseTextures[]             : register(t0, space3);
Texture2D<float4> EffectTextures[]              : register(t0, space4);

#ifdef SHARC
RWStructuredBuffer<uint64_t>                u_SharcHashEntriesBuffer    : register(u0, space3);
RWStructuredBuffer<SharcAccumulationData>   u_SharcAccumulationBuffer   : register(u1, space3);
RWStructuredBuffer<SharcPackedData>         u_SharcResolvedBuffer       : register(u2, space3);
#endif

SamplerState DiffuseSampler                     : register(s0);
//SamplerState GlowSampler                        : register(s1);

RWTexture2D<float4> DiffuseOutputTexture        : register(u0);
RWTexture2D<float4> SpecularOutputTexture       : register(u1);

#endif