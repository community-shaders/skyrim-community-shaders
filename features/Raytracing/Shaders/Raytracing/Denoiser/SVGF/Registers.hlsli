#ifndef SVGF_REGISTERS_HLSI
#define SVGF_REGISTERS_HLSI

#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

ConstantBuffer<SVGF> Frame              : register(b0);

RWTexture2D<float4> OutputTexture       : register(u0);

Texture2D<float4> ReflectanceTexture    : register(t0);
Texture2D<float> SpecularHitDist        : register(t1);

Texture2D<float4> GeometryNormalDepthTexture: register(t2);
Texture2D<float4> AlbedoTexture             : register(t3);
Texture2D<unorm float> DepthTexture         : register(t4);

#endif