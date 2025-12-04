#ifndef GEOMETRY_HLSI
#define GEOMETRY_HLSI

float3 GetBary(BuiltInTriangleIntersectionAttributes attribs)
{
    return float3(
        1.0f - attribs.barycentrics.x - attribs.barycentrics.y, 
        attribs.barycentrics.x, 
        attribs.barycentrics.y
    );
}

float2 Interpolate(half2 u, half2 v, half2 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

float3 Interpolate(float3 u, float3 v, float3 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

float3 Interpolate(half3 u, half3 v, half3 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

float4 Interpolate(half4 u, half4 v, half4 w, float3 uvw)
{
    return u * uvw.x + v * uvw.y + w * uvw.z;
}

Instance GetInstance()
{
    return Instances[InstanceIndex()];
}

uint GetMeshID()
{
    return InstanceID() + GeometryIndex();
}

Triangle GetTriangle(uint meshID)
{
    return Triangles[meshID][PrimitiveIndex()];
}

void GetVertices(uint meshID, out Vertex v0, out Vertex v1, out Vertex v2)
{
    Triangle geomTriangle = GetTriangle(meshID);
    
    StructuredBuffer<Vertex> geomVertices = Vertices[meshID];    
    v0 = geomVertices[geomTriangle.x];
    v1 = geomVertices[geomTriangle.y];
    v2 = geomVertices[geomTriangle.z];
    
    /*v0 = Vertices[meshID][geomTriangle.x];
    v1 = Vertices[meshID][geomTriangle.y];
    v2 = Vertices[meshID][geomTriangle.z];*/   
}

#endif // GEOMETRY_HLSI