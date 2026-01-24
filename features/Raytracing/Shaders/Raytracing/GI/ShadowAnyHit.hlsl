#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"
#include "Raytracing/Includes/PBR.hlsli"

#include "Common/Color.hlsli"

[shader("anyhit")]
void main(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    uint shapeIdx = GetShapeIdx(InstanceIndex(), GeometryIndex());

    Vertex v0, v1, v2;
    GetVertices(shapeIdx, PrimitiveIndex(), v0, v1, v2);

    float3 uvw = GetBary(attribs.barycentrics);

    Material material = Materials[shapeIdx];

    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    float alpha;
    const bool isWindows = material.Feature == Feature::kGlowMap || material.PBRFlags & PBR::Flags::HasEmissive;
    
    [branch]
    if (isWindows)
        alpha = 1.0f - Color::RGBToLuminance(Textures[NonUniformResourceIndex(material.GlowTexture())].SampleLevel(BaseSampler, texCoord, 0).rgb);
    else
        alpha = Textures[NonUniformResourceIndex(material.BaseTexture())].SampleLevel(BaseSampler, texCoord, 0).a;

    [branch]
    if (material.AlphaFlags == AlphaFlags::kAlphaTest || isWindows)
    {
        if (alpha < 0.5f)
        {
            IgnoreHit();
        }
        else
        {
            AcceptHitAndEndSearch();
        }
    }
    else if (material.AlphaFlags == AlphaFlags::kAlphaBlend)
    {
        float rnd = Random(payload.randomSeed);
        if (rnd > alpha)
        {
            IgnoreHit();
        }
        else
        {
            AcceptHitAndEndSearch();
        }
    }
}

