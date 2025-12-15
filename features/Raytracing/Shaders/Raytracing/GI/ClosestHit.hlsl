#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

#include "Raytracing/Includes/Common.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"
#include "Raytracing/Includes/RT/Rays.hlsli"
#include "Raytracing/Includes/RT/Shading.hlsli"

#include "Common/Game.hlsli"
#include "Common/Color.hlsli"

void HitMesh(inout IndirectPayload payload, in BuiltInTriangleIntersectionAttributes attribs);

[shader("closesthit")]
void main(inout IndirectPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    HitMesh(payload, attribs);
}

void HitMesh(inout IndirectPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    Instance instance = GetInstance();
    uint meshID = GetMeshID();
    
    Vertex v0, v1, v2;
    GetVertices(meshID, v0, v1, v2);

    float3 uvw = GetBary(attribs);
    
    Material material = Materials[meshID];
    
    float2 texCoord = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
    
    float3x3 objectToWorld3x3 = (float3x3) ObjectToWorld3x4();
    
    float3 geomWorldNormal = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
    float3 geomWorldTangent = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
    float3 geomWorldBitangent = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
    
    float3x3 TBN = float3x3(geomWorldTangent, geomWorldBitangent, geomWorldNormal);
    
    float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);
    
    unorm float roughnessSrc = DEFAULT_ROUGHNESS;
    unorm float metalnessSrc = DEFAULT_METALNESS;
    unorm float ao = 1.0f;
    
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture)];
    Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture)];
    
#ifdef PATH_TRACING     
    Texture2D normalTexture = Textures[NonUniformResourceIndex(material.NormalTexture)];
    Texture2D rmaosTexture = Textures[NonUniformResourceIndex(material.RMAOSTexture)];
#endif
    
    float3 base = baseTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
    float3 effect = effectTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
    
#ifdef PATH_TRACING
    float3 normal = normalTexture.SampleLevel(BaseSampler, texCoord, 0).rgb * 2.0f - 1.0f;  
    float4 rmaos = rmaosTexture.SampleLevel(BaseSampler, texCoord, 0);
    
    // Normal mapping
    float tangentSign = (dot(cross(geomWorldNormal, geomWorldTangent), geomWorldBitangent) < 0.0f) ? -1.0f : 1.0f; 
    
    float3 worldNormal = normalize(mul(normal, TBN));  
    float3 worldTangent = normalize(geomWorldTangent - worldNormal * dot(geomWorldTangent, worldNormal)); 
    float3 worldBitangent = cross(worldNormal, worldTangent) * tangentSign;   
    
    // Normal mapped TBN
    TBN = float3x3(worldTangent, worldBitangent, worldNormal);
    
    // Roughness and Metalness from RMAOS
    roughnessSrc = saturate(rmaos.x * material.roughness);
    metalnessSrc = saturate(rmaos.y);
    ao = rmaos.z;
#else
    float3 worldNormal = geomWorldNormal;
#endif
    
    // Lighting Shader
    float3 lightingAlbedo = Color::GammaToLinear(base) * material.BaseColor.rgb * vertexColor.rgb;
    float3 lightingEmissive = Color::GammaToLinear(effect) * material.EffectColor.rgb * material.EffectColor.a;
 
    // Effect Shader
    /*float3 baseColorMul = material.EffectColor.rgb * vertexColor.rgb;
    float3 baseColor = diffuse.rgb * baseColorMul;

    float baseColorScale = material.EffectColor.a;
    float2 grayscaleToColorUv = float2(diffuse.y, baseColorMul.x);
    
    baseColor = baseColorScale * effectTexture.SampleLevel(DiffuseSampler, grayscaleToColorUv, 0).rgb;
   
    float3 effectAlbedo = Color::GammaToLinear(baseColor.xyz);
    float3 effectEmissive = baseColor * Frame.Effect;
    
    float3 albedo = lerp(lightingAlbedo, effectAlbedo, material.ShaderType);
    float3 emissive = lerp(lightingEmissive, effectEmissive, material.ShaderType);*/
    
    //float3 albedo = Color::Diffuse(diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb);
    //float3 emissive = Color::Diffuse(effectTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb) * material.EffectColor.rgb * material.EffectColor.a; 
  
    float3 albedo = lightingAlbedo;
    float3 emissive = lightingEmissive;
    
#if !defined(LAMBERT)
    float3 viewDirection = normalize(-WorldRayDirection());
    
    const unorm float perceptualRoughness = clamp(Scale01(roughnessSrc, Frame.Roughness.x, Frame.Roughness.y), MIN_ROUGHNESS, MAX_ROUGHNESS);
    const unorm float roughness = perceptualRoughness * perceptualRoughness;
    
    const unorm float metalness = Scale01(metalnessSrc, Frame.Metalness.x, Frame.Metalness.y);
#endif
    
    uint randomSeed = payload.data.GetSeed();

    payload.color += float4(emissive * Frame.Emissive, 0.0f);
    
    // Directional Light
#if defined(LAMBERT)
    payload.color += float4(LambertianDirectD(worldPosition, worldNormal, albedo, Frame.Directional, randomSeed), 0.0f);
#else
    payload.color += float4(GGXDirectD(worldPosition, worldNormal, viewDirection, albedo, roughness, metalness, Frame.Directional, randomSeed), 0.0f);
#endif
    
    [unroll]
    for (uint i = 0; i < SAMPLES; i++)
    {
#if defined(LAMBERT)
        payload.color += float4(LambertianDirectP(worldPosition, worldNormal, albedo, instance.LightData, randomSeed), 0.0f);
#else
        payload.color += float4(GGXDirectP(worldPosition, worldNormal, viewDirection, albedo, roughness, metalness, instance.LightData, randomSeed), 0.0f);
#endif
    
        uint currentDepth = payload.data.GetDepth();
    
        if (currentDepth < MAX_DEPTH)
        {
#if defined(LAMBERT)
            payload.color += float4(LambertianIndirect(worldPosition, worldNormal, albedo, currentDepth, randomSeed), 0.0f);
#else
            float4 indirect = GGXIndirect(worldPosition, geomWorldNormal, TBN, viewDirection, albedo, roughness, metalness, ao, currentDepth, randomSeed);
            indirect.a = max(payload.color.a, RayTCurrent()); // * (1.0f - saturate(abs(currentDepth - 1.0f))); // 0,1,2,... to -1,0,1,... to 1,0,1 to 0, 1, 0
        
            payload.color += indirect;
#endif 
        }
    }
}
