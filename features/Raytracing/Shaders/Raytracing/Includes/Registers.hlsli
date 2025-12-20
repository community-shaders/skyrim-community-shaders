#ifndef REGISTERS_HLSL
#define REGISTERS_HLSL

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/SHaRC.hlsli"

ConstantBuffer<FrameData> Frame                 : register(b0);

RWTexture2D<float4> OutputTexture               : register(u0);
RWTexture2D<float4> SpecularAlbedo              : register(u1);
RWTexture2D<float> SpecularHitDist              : register(u2);

#ifdef SHARC
RWStructuredBuffer<uint64_t>                u_SharcHashEntriesBuffer    : register(u3);
RWStructuredBuffer<uint>                    u_SharcLockBuffer           : register(u4);
RWStructuredBuffer<SharcAccumulationData>   u_SharcAccumulationBuffer   : register(u5);
RWStructuredBuffer<SharcPackedData>         u_SharcResolvedBuffer       : register(u6);
#endif

Texture2D<float4> MainTexture                   : register(t0, space0); // RENDER_TARGETS::kMAIN
Texture2D<float> DepthTexture                   : register(t1, space0); // RENDER_TARGETS_DEPTHSTENCIL::kMAIN - R32
Texture2D<float4> AlbedoTexture                 : register(t2, space0); // ALBEDO - True albedo (not modulated by metalness)
Texture2D<snorm float4> NormalRoughnessTexture  : register(t3, space0); // "NORMALROUGHNESS" - World normals and roughness - Processed from GBuffer encoded view normals and smoothness
Texture2D<unorm float4> GNMAOTexture            : register(t4, space0); // MASKS2 - Geometry normals (Encoded) + metalness/AO (Packed)

RaytracingAccelerationStructure Scene           : register(t5, space0);
Texture2D<float4> SkyHemisphere                 : register(t6, space0);
StructuredBuffer<Light> Lights                  : register(t7, space0);
StructuredBuffer<Material> Materials            : register(t8, space0);
StructuredBuffer<Instance> Instances            : register(t9, space0);

StructuredBuffer<Vertex> Vertices[]             : register(t0, space1);
StructuredBuffer<Triangle> Triangles[]          : register(t0, space2);
Texture2D<float4> Textures[]                    : register(t0, space3);

SamplerState BaseSampler                        : register(s0);
//SamplerState EffectSampler                      : register(s1);

#endif