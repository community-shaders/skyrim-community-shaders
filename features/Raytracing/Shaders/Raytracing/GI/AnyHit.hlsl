#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"
#include "Raytracing/Includes/PBR.hlsli"

#include "Common/Color.hlsli"

[shader("anyhit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    Shape shape = GetShape(InstanceIndex(), GeometryIndex());

    Vertex v0, v1, v2;
    GetVertices(shape.GeometryIdx, PrimitiveIndex(), v0, v1, v2);

    float3 uvw = GetBary(attribs.barycentrics);

    Material material = shape.Material;

    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    float alpha = Textures[NonUniformResourceIndex(material.BaseTexture())].SampleLevel(BaseSampler, texCoord, 0).a;

    [branch]
    if (material.AlphaFlags == AlphaFlags::kAlphaTest)
    {
        if (alpha < 0.5f)
        {
            IgnoreHit();
        }
    }
    else if (material.AlphaFlags == AlphaFlags::kAlphaBlend)
    {
        float rnd = Random(payload.randomSeed);
        if (rnd > alpha)
        {
            IgnoreHit();
        }
    }
}

