#ifndef GEOMETRY_HLSL
#define GEOMETRY_HLSL

#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/Types.hlsli"

float3 GetBary(float2 barycentrics)
{
    return float3(
        1.0f - barycentrics.x - barycentrics.y, 
        barycentrics.x, 
        barycentrics.y
    );
}

inline float2 Interpolate(half2 u, half2 v, half2 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float3 Interpolate(float3 u, float3 v, float3 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float3 Interpolate(half3 u, half3 v, half3 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

inline float4 Interpolate(half4 u, half4 v, half4 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

Instance GetInstance(uint instanceIdx)
{
    return Instances[instanceIdx];
}

uint GetShapeIdx(in Payload payload, out Instance instance)
{
    instance = GetInstance(payload.InstanceIndex());   
    return Indirection.Load((instance.FirstGeometryID + payload.GeometryIndex()) * 4);
}

uint GetShapeIdx(in uint instanceIndex, in uint geometryIndex)
{
    Instance instance = GetInstance(instanceIndex);   
    return Indirection.Load((instance.FirstGeometryID + geometryIndex) * 4);
}

Triangle GetTriangle(in uint shapeIdx, in uint primitiveIdx)
{
    return Triangles[shapeIdx][primitiveIdx];
}

void GetVertices(in uint shapeIndex, in uint primitiveIndex, out Vertex v0, out Vertex v1, out Vertex v2)
{
    Triangle geomTriangle = GetTriangle(shapeIndex, primitiveIndex);
    
    StructuredBuffer<Vertex> vertices = Vertices[shapeIndex];    
    v0 = vertices[geomTriangle.x];
    v1 = vertices[geomTriangle.y];
    v2 = vertices[geomTriangle.z];  
}

#endif // GEOMETRY_HLSL