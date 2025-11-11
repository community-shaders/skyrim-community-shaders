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

    float3 albedo = Color::GammaToLinear(diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb);
    
    uint randomSeed = payload.data.GetSeed();
    
    // Directional Light
    float3 directLighting = 0.0f;
    {
        Light directionalLight = Frame.Directional;
        
        float NdotL = saturate(dot(worldNormal, directionalLight.Vector));

        // Shadow
        {
            RayDesc shadowRay;
            shadowRay.Origin = worldPosition + worldNormal * 0.1f;
            shadowRay.Direction = directionalLight.Vector;
            shadowRay.TMin = 0.0001f;
            shadowRay.TMax = 1e30;

            ShadowPayload shadowPayload;
            shadowPayload.missed = false;
        
            TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 1, 0, 1, shadowRay, shadowPayload);    
    
            NdotL *= TraceRayShadow(Scene, worldPosition, directionalLight.Vector, randomSeed);
        }
            
        directLighting = NdotL * directionalLight.Color;
    }
    
    uint currentDepth = payload.data.GetDepth();
    
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
            
        float fade = saturate(1.0 - pow(lightDistance / pointLight.Range, 4.0));
            
        float NdotL = saturate(dot(worldNormal, lightVector)) * (1.0 / max(lightDistanceSqr, 0.01)) * fade * fade;
        
        // Shadow
        if(currentDepth < SHADOW_MAX_DEPTH)
        {
            NdotL *= TraceRayShadow(Scene, worldPosition, lightVector, randomSeed);
        }
            
        directLighting += NdotL * pointLight.Color;       
    }
    
    // Bounce
    #ifdef SPECULAR
    float4 indirectLight = 0.0f;
    #else
    float3 indirectLight = 0.0f;
    #endif
    {
        if (currentDepth < MAX_DEPTH)
        {
            /*[unroll]
            for(uint i = 0; i < 2; i++) 
            {*/
            #ifdef SPECULAR
            indirectLight += TraceRaySpecular(Scene, worldPosition, worldNormal, 0, currentDepth, randomSeed);
            #else
            indirectLight += TraceRayDiffuse(Scene, worldPosition, worldNormal, currentDepth, randomSeed);
            #endif
            //}
        }
    }
    
    #ifdef SPECULAR
    payload.distance = indirectLight.a;
    #endif
    
    payload.color = albedo * directLighting + albedo * indirectLight.rgb;
}
