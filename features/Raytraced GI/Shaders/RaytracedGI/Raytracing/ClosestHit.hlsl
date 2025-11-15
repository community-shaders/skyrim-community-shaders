#include "RaytracedGI/Includes/Types.hlsli"
#include "RaytracedGI/Includes/Common.hlsli"
#include "Common/Game.hlsli"
#include "Common/Color.hlsli"

#include "RaytracedGI/Includes/Registers.hlsli"

#ifdef SPECULAR
typedef SpecularPayload CurrentPayload;
#else
typedef DiffusePayload CurrentPayload;
#endif

void HitMesh(inout CurrentPayload payload, in BuiltInTriangleIntersectionAttributes attribs);

[shader("closesthit")]
void main(inout CurrentPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    HitMesh(payload, attribs);
}

void HitMesh(inout CurrentPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();  

    Instance instance = Instances[InstanceID()];
    uint meshID = NonUniformResourceIndex(instance.MeshID);
    
    StructuredBuffer<Vertex> meshVertices = Vertices[meshID];
    StructuredBuffer<uint3> meshTriangles = Triangles[meshID];
    Texture2D diffuseTexture = DiffuseTextures[meshID];
    Texture2D glowTexture = GlowTextures[meshID];
    
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

    Material material = instance.Material;
    
    texCoord0 += material.texCoordOffsetScale.xy;
    texCoord0 *= material.texCoordOffsetScale.zw;
    
    float3 albedo = Color::Diffuse(diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb);
    float3 emissive = Color::Diffuse(glowTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb) * material.emissiveColor.rgb * material.emissiveColor.a;
    
    uint randomSeed = payload.data.GetSeed();
    
    Light directionalLight = Frame.Directional;
    
    // Directional Light   
    float NdotL = saturate(dot(worldNormal, directionalLight.Vector));
    NdotL *= TraceRayShadow(Scene, worldPosition, directionalLight.Vector, randomSeed);
    
    float3 directLighting = NdotL * directionalLight.Color;

    //uint currentDepth = payload.data.GetDepth();
    
    // Point lights
    LightData lightData = instance.LightData;
    for(uint i = 0; i < lightData.Count; i++)
    {
        uint lightID = lightData.GetID(i);
        Light pointLight = Lights[lightID];
        
        float3 lightVector = (pointLight.Vector - worldPosition) * GAME_UNIT_TO_M;
        float lightDistanceSqr = dot(lightVector, lightVector);
        float lightDistance = sqrt(lightDistanceSqr);
        
        lightVector /= lightDistance;
         
        float attenuation = 1.0 / max(lightDistanceSqr, 0.01);
        float fade = saturate(1.0 - pow(lightDistance / pointLight.Range, 4.0));
            
        float NdotL = saturate(dot(worldNormal, lightVector)) * attenuation * fade * fade;
        
        // Shadow
        //if(currentDepth < SHADOW_MAX_DEPTH)
        //{
            NdotL *= TraceRayShadowFinite(Scene, worldPosition, lightVector, lightDistance * M_TO_GAME_UNIT, randomSeed);
        //}
            
        directLighting += NdotL * pointLight.Color;       
    }
    
    // Bounce
    /*#ifdef SPECULAR
    float4 indirectLight = 0.0f;
    #else
    float3 indirectLight = 0.0f;
    #endif
    {
        if (currentDepth < MAX_DEPTH)
        {
            //[unroll]
            //for(uint i = 0; i < 2; i++) 
            //{
            #ifdef SPECULAR
            indirectLight += TraceRaySpecular(Scene, worldPosition, worldNormal, currentDepth, randomSeed, Frame.Specular, 0);
            #else
            indirectLight += TraceRayDiffuse(Scene, worldPosition, worldNormal, currentDepth, randomSeed, Frame.Diffuse);
            #endif
            //}
        }
    }*/
    
    
#ifndef SPECULAR
    uint currentDepth = payload.data.GetDepth();
    
    if (currentDepth < MAX_DEPTH)
    {
        directLighting += TraceRayDiffuse(Scene, worldPosition, worldNormal, currentDepth, randomSeed, Frame.Diffuse);
    } 
#endif
    
    payload.color = albedo * directLighting + emissive * Frame.Emissive;
    payload.color *= saturate(-dot(worldNormal, WorldRayDirection()));
    
    #ifdef SPECULAR
    payload.distance = RayTCurrent();
    #endif    
}
