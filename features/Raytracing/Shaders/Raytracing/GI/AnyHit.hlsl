#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

#include "Common/Color.hlsli"

[shader("anyhit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    uint shapeIdx = GetShapeIdx();
   
    Vertex v0, v1, v2;
    GetVertices(shapeIdx, PrimitiveIndex(), v0, v1, v2);
    
    float3 uvw = GetBary(attribs.barycentrics);
    
    Material material = Materials[shapeIdx];
    
    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    float alpha = Textures[NonUniformResourceIndex(material.BaseTexture)].SampleLevel(BaseSampler, texCoord, 0).a;
    
    if (alpha < 0.5f)
    {
        IgnoreHit();
    }    
}

