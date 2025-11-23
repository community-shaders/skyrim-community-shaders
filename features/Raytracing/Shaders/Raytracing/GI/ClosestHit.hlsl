#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Registers.hlsli"

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

    Instance instance = Instances[InstanceID()];
    uint meshID = NonUniformResourceIndex(instance.MeshID);
    
    StructuredBuffer<Vertex> meshVertices = Vertices[meshID];
    StructuredBuffer<uint3> meshTriangles = Triangles[meshID];
    Texture2D diffuseTexture = DiffuseTextures[meshID];
    Texture2D effectTexture = EffectTextures[meshID];
    
    uint3 meshTriangle = meshTriangles[PrimitiveIndex()];
    
    Vertex vertice0 = meshVertices[meshTriangle.x];
    Vertex vertice1 = meshVertices[meshTriangle.y];
    Vertex vertice2 = meshVertices[meshTriangle.z];
    
    float v = attribs.barycentrics.x;
    float w = attribs.barycentrics.y;
    float u = 1.0 - v - w;
      
    half2 texCoord0 = vertice0.Texcoord0.unpack() * u + vertice1.Texcoord0.unpack() * v + vertice2.Texcoord0.unpack() * w;
    
    half4 normal0 = vertice0.Normal.unpack();
    half4 normal1 = vertice1.Normal.unpack();
    half4 normal2 = vertice2.Normal.unpack();
     
    half3 normal = normalize(normal0.xyz * u + normal1.xyz * v + normal2.xyz * w); 
    half3 worldNormal = normalize(mul((float3x3)ObjectToWorld3x4(), normal));

    half4 vertexColor = vertice0.Color.unpack() * u + vertice1.Color.unpack() * v + vertice2.Color.unpack() * w;
    
    Material material = instance.Material;
    
    texCoord0 += material.TexCoordOffsetScale.xy;
    texCoord0 *= material.TexCoordOffsetScale.zw;
    
    float3 diffuse = diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb;
    float3 effect = effectTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb;
    
    // Lighting Shader
    float3 lightingAlbedo = Color::GammaToLinear(diffuse) * vertexColor.rgb;
    float3 lightingEmissive = Color::GammaToLinear(effect) * material.EffectColor.rgb * material.EffectColor.a;
    
    // Effect Shader
    float3 baseColorMul = material.EffectColor.rgb * vertexColor.rgb;
    float3 baseColor = diffuse.rgb * baseColorMul;

    float baseColorScale = material.EffectColor.a;
    float2 grayscaleToColorUv = float2(diffuse.y, baseColorMul.x);
    
    baseColor = baseColorScale * effectTexture.SampleLevel(DiffuseSampler, grayscaleToColorUv, 0).rgb;
   
    float3 effectAlbedo = Color::GammaToLinear(baseColor.xyz);
    float3 effectEmissive = baseColor * Frame.Effect;
    
    float3 albedo = lerp(lightingAlbedo, effectAlbedo, material.ShaderType);
    float3 emissive = lerp(lightingEmissive, effectEmissive, material.ShaderType);
    
    //float3 albedo = Color::Diffuse(diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb);
    //float3 emissive = Color::Diffuse(effectTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb) * material.EffectColor.rgb * material.EffectColor.a; 
    
    const unorm float metalness = Scale01(DEFAULT_METALNESS, Frame.Metalness.x, Frame.Metalness.y);
	const unorm float roughness = max(Scale01(DEFAULT_ROUGHNESS, Frame.Roughness.x, Frame.Roughness.y), MIN_ROUGHNESS);      
    
    uint randomSeed = payload.data.GetSeed();
    
    //float3 viewDirection = normalize(Frame.Position - worldPosition);
    float3 viewDirection = normalize(-WorldRayDirection());
    
    // Directional Light
    payload.color += float4(GGXDirectD(worldPosition, worldNormal, viewDirection, albedo, DEFAULT_SPECULAR, roughness, Frame.Directional), 0.0f);
    
    #if defined(LAMBERT)
    payload.color += LambertianDirect(worldPosition, worldNormal, albedo, instance.LightData, randomSeed);
    #else
    payload.color += float4(GGXDirectP(worldPosition, worldNormal, viewDirection, albedo, DEFAULT_SPECULAR, roughness, instance.LightData, randomSeed), 0.0f);
    #endif
    
    uint currentDepth = payload.data.GetDepth();
    
    if (currentDepth < MAX_DEPTH)
    {
        #if defined(LAMBERT)
        payload.color += LambertianIndirect(worldPosition, worldNormal, albedo, currentDepth, randomSeed);
        #else
        float4 indirect = GGXIndirect(worldPosition, worldNormal, worldNormal, viewDirection, albedo, DEFAULT_SPECULAR, roughness, metalness, currentDepth, randomSeed);
        indirect.a = RayTCurrent();// * (1.0f - saturate(abs(currentDepth - 1.0f))); // 0,1,2,... to -1,0,1,... to 1,0,1 to 0, 1, 0
        
        payload.color += indirect;
        #endif        
    }    
}
