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
    
    float3x3 objectToWorld3x3 = (float3x3)ObjectToWorld3x4();
    
    float3 worldNormal = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
    float3 worldTangent = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
    float3 worldBitangent = normalize(mul(objectToWorld3x3, Interpolate(v0.Bitangent, v1.Bitangent, v2.Bitangent, uvw)));
    
    float3x3 TBN = float3x3(worldTangent, worldBitangent, worldNormal);
    
    float4 vertexColor = Interpolate(v0.Color.unpack(), v1.Color.unpack(), v2.Color.unpack(), uvw);
    
    Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture)];
    Texture2D effectTexture = Textures[NonUniformResourceIndex(material.EffectTexture)];
    
    float3 base = baseTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
    float3 effect = effectTexture.SampleLevel(BaseSampler, texCoord, 0).rgb;
    
    // Lighting Shader
    // These need to be redone
    /*float3 lightingAlbedo = Color::GammaToLinear(base) * vertexColor.rgb;
    float3 lightingEmissive = Color::GammaToLinear(effect) * material.EffectColor.rgb * material.EffectColor.a;
    
    // Effect Shader
    float3 baseColorMul = material.EffectColor.rgb * vertexColor.rgb;
    float3 baseColor = base.rgb * baseColorMul;

    float baseColorScale = material.EffectColor.a;
    float2 grayscaleToColorUv = float2(base.y, baseColorMul.x);
    
    baseColor = baseColorScale * effectTexture.SampleLevel(BaseSampler, grayscaleToColorUv, 0).rgb;
   
    float3 effectAlbedo = Color::GammaToLinear(baseColor.xyz);
    float3 effectEmissive = baseColor * Frame.Effect;
    
    float3 albedo = lerp(lightingAlbedo, effectAlbedo, material.ShaderType);
    float3 emissive = lerp(lightingEmissive, effectEmissive, material.ShaderType);*/
    
    //float3 albedo = Color::Diffuse(diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb);
    //float3 emissive = Color::Diffuse(effectTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb) * material.EffectColor.rgb * material.EffectColor.a; 
    
    float3 albedo = Color::GammaToLinear(base);
    //float3 emissive = Color::GammaToLinear(effect) * material.EffectColor.rgb * material.EffectColor.a; 
    
    #if !defined(LAMBERT)
    float3 viewDirection = normalize(-WorldRayDirection());
    
    const unorm float metalness = Scale01(DEFAULT_METALNESS, Frame.Metalness.x, Frame.Metalness.y);
    
    const unorm float perceptualRoughness = DEFAULT_ROUGHNESS;
    
	const unorm float roughness = max(Scale01(perceptualRoughness * perceptualRoughness, Frame.Roughness.x, Frame.Roughness.y), MIN_ROUGHNESS);      
    #endif
    
    uint randomSeed = payload.data.GetSeed();

    // Directional Light
    #if defined(LAMBERT)
    payload.color += float4(LambertianDirectD(worldPosition, worldNormal, albedo, Frame.Directional), 0.0f);
    #else
    payload.color += float4(GGXDirectD(worldPosition, worldNormal, viewDirection, albedo, roughness, metalness, Frame.Directional), 0.0f);
    #endif
    
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
        float4 indirect = GGXIndirect(worldPosition, worldNormal, TBN, worldNormal, viewDirection, albedo, DEFAULT_SPECULAR, roughness, metalness, currentDepth, randomSeed);
        indirect.a = RayTCurrent();// * (1.0f - saturate(abs(currentDepth - 1.0f))); // 0,1,2,... to -1,0,1,... to 1,0,1 to 0, 1, 0
        
        payload.color += indirect;
        #endif        
    }
}
