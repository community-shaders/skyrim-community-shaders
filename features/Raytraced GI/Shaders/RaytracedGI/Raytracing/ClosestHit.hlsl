#include "RaytracedGI/Raytracing/Types.hlsli"
#include "RaytracedGI/Raytracing/Common.hlsli"
#include "Common/Game.hlsli"

ConstantBuffer<FrameData> Frame         : register(b0);

SamplerState DiffuseSampler             : register(s0);

RaytracingAccelerationStructure Scene   : register(t0, space0);
StructuredBuffer<Light> Lights          : register(t1, space0);
StructuredBuffer<Instance> Instances    : register(t2, space0);

StructuredBuffer<Vertex> Vertices[]     : register(t0, space1);
StructuredBuffer<uint3> Triangles[]     : register(t0, space2);
Texture2D DiffuseTextures[]             : register(t0, space3);

static const float scale = GAME_UNIT_TO_M;
static const float blendSharpness = 4.0f;

float checker(float2 uv)
{
    float2 c = floor(uv);
    return fmod(c.x + c.y, 2.0); // 0 or 1 alternating
}

float3 Triplanar(float3 worldPos, float3 worldNormal)
{
    // Compute blend weights based on normal
    float3 n = abs(worldNormal);
    n = pow(n, blendSharpness);
    n /= (n.x + n.y + n.z + 1e-5); // Normalize

    // Project UVs onto each plane
    float2 uvX = worldPos.yz * scale;
    float2 uvY = worldPos.xz * scale;
    float2 uvZ = worldPos.xy * scale;

    // Checkerboard pattern from each projection
    float xCheck = checker(uvX);
    float yCheck = checker(uvY);
    float zCheck = checker(uvZ);

    // Blend them together
    float blended = xCheck * n.x + yCheck * n.y + zCheck * n.z;

    // Optional: make it binary for sharper edges
    return lerp(0.6f, 0.4f, blended).rrr;
}

void HitMesh(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs);

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    HitMesh(payload, attribs);
}


void HitMesh(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
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

    //float3 albedo = Triplanar(worldPosition, worldNormal);
    float3 albedo = diffuseTexture.SampleLevel(DiffuseSampler, texCoord0, 0).rgb;
    
    // Directional Light
    float3 lighting = 0.0f;
    {
        Light directionalLight = Frame.Directional;
        
        float NdotL = saturate(dot(worldNormal, directionalLight.Vector));

        // Shadow
        {
            RayDesc shadowRay;
            shadowRay.Origin = worldPosition + worldNormal * 0.1f;
            shadowRay.Direction = directionalLight.Vector;
            shadowRay.TMin = 0.1f;
            shadowRay.TMax = 1e30;

            ShadowPayload shadowPayload;
            shadowPayload.missed = false;
        
            TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 1, 0, 1, shadowRay, shadowPayload);    
    
            NdotL *= saturate(shadowPayload.missed);
        }
            
        lighting = NdotL * directionalLight.Color;
    }
    
    // Bounce
    {
        uint currentDepth = payload.data.GetDepth();

        if (currentDepth < MAX_DEPTH)
        {
            uint randomSeed = payload.data.GetSeed();
            float3 bounceDir = SampleHemisphere(worldNormal, randomSeed);
            
            RayDesc bounceRay;
            bounceRay.Origin = worldPosition + worldNormal * 0.1f;
            bounceRay.Direction = bounceDir;
            bounceRay.TMin = 0.1f;
            bounceRay.TMax = 1e30;
    
            Payload bouncePayload;
            bouncePayload.color = float3(0, 0, 0);
            bouncePayload.data = PayloadData::Create(false, currentDepth + 1, randomSeed);

            TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, bounceRay, bouncePayload);
            lighting += bouncePayload.color;
        }
    }
    
    payload.color = albedo * lighting;
}
