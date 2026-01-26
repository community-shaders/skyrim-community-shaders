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

    float alpha = Textures[NonUniformResourceIndex(material.BaseTexture())].SampleLevel(BaseSampler, texCoord, 0).a;

    [branch]
    if (material.AlphaFlags == AlphaFlags::kAlphaTest)
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
    else if ((material.Feature == Feature::kGlowMap || material.PBRFlags & PBR::Flags::HasEmissive) && material.ShaderFlags & ShaderFlags::kAssumeShadowmask) // only window for now
    {
        float3 transmittance = 0.0f;
        float3 F0 = 0.04f;
        [branch]
        if (material.Feature == Feature::kGlowMap)
        {
            transmittance = Textures[NonUniformResourceIndex(material.GlowTexture())].SampleLevel(BaseSampler, texCoord, 0).rgb;
            [branch]
            if (material.ShaderFlags & ShaderFlags::kSpecular) {
                Roughness = material.RoughnessScale() >= 0.0f ? saturate(material.RoughnessScale()) : 1.0f;

                float3 specularColor = 0.0f;

                [branch]
                if (material.ShaderFlags & ShaderFlags::kModelSpaceNormals) {
                    Texture2D specularTexture = Textures[NonUniformResourceIndex(material.SpecularTexture())];
                    specularColor = specularTexture.SampleLevel(BaseSampler, texCoord, 0).r * material.SpecularColor().rgb * material.SpecularColor().a;
                } else {
                    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
                    specularColor = normalTexture.SampleLevel(BaseSampler, texCoord, 0).a * material.SpecularColor().rgb * material.SpecularColor().a;
                }
                F0 = clamp(0.08f * specularColor, 0.02f, 0.08f);
            }
        }
        else
        {
            Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture())];
            Texture2D emissiveTexture = Textures[NonUniformResourceIndex(material.EmissiveTexture())];
            float specular = rmaosTexture.SampleLevel(BaseSampler, texCoord, 0).a;
            float3 emissive = emissiveTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
            transmittance = emissive;
            F0 = material.SpecularLevel() * specular;
        }

        Instance instance = GetInstance(InstanceIndex());
        float3x3 objectToWorld3x3 = (float3x3) instance.Transform;

        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
        float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
        float3 bitangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));

        Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture())];
        float3 normal = normalTexture.SampleLevel(BaseSampler, texCoord, 0).xyz * 2.0f - 1.0f;

        float handedness = (dot(cross(normalWS, tangentWS), bitangentWS) < 0.0f) ? -1.0f : 1.0f;

        NormalMap(
                normal,
                handedness,
                normalWS, tangentWS, bitangentWS,
                Normal, Tangent, Bitangent
            );

        float3 viewDir = -Normalize(WorldRayDirection());

        float NdotV = abs(dot(Normal, viewDir));

        float3 F = BRDF::F_Schlick(F0, NdotV);
        transmittance *= (1.0f - F) / (1.0f + F);

        payload.transmission *= transmittance;
        IgnoreHit();
    }
}

