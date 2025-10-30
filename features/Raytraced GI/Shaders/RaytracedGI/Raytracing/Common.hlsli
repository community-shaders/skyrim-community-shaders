#ifndef COMMON_HLSI
#define COMMON_HLSI

#include "RaytracedGI/Raytracing/Payload.hlsli"
#include "RaytracedGI/Raytracing/Vertex.hlsli"
#include "RaytracedGI/Raytracing/Light.hlsli"

cbuffer FrameBuffer : register(b0)
{
    float4x4 ViewInverse;
    float4x4 ProjInverse;
    float4 Position;
    Light Directional;
}

#endif