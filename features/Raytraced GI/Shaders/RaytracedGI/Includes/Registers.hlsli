#ifndef REGISTERS_HLSI
#define REGISTERS_HLSI

#include "RaytracedGI/Includes/Types.hlsli"

ConstantBuffer<GIFrameData> Frame               : register(b0);

RWTexture2D<float4> OutputTexture               : register(u0);
RWTexture2D<float4> ReflectanceTexture          : register(u1);
RWTexture2D<float> SpecularHitDist              : register(u2);

Texture2D<float4> MainTexture                   : register(t0, space0);
Texture2D<float> DepthTexture                   : register(t1, space0);
Texture2D<float4> AlbedoTexture                 : register(t2, space0);
Texture2D<snorm float4> NormalRoughnessTexture  : register(t3, space0);
Texture2D<float4> GNMXTexture                   : register(t4, space0);

RaytracingAccelerationStructure Scene           : register(t5, space0);
Texture2D<float3> SkyHemisphere                 : register(t6, space0);
StructuredBuffer<Light> Lights                  : register(t7, space0);
StructuredBuffer<Instance> Instances            : register(t8, space0);

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

#endif