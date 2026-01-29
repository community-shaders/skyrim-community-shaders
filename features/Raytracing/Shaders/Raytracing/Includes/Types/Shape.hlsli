#ifndef SHAPE_HLSL
#define SHAPE_HLSL

#include "Raytracing/Includes/Types/Material.hlsli"

#ifdef __cplusplus
struct ShapeData
{
    MaterialData Material;
    uint GeometryIdx;
    uint2 Pad0;
	float3x4 Transform;
};

static_assert(sizeof(ShapeData) % 4 == 0);

#else
struct Shape
{
    Material Material;   
    uint GeometryIdx;
    uint2 Pad0;
    row_major float3x4 Transform;
};
#endif

#endif