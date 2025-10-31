#include "RaytracedGI/Raytracing/Common.hlsli"
#include "RaytracedGI/Raytracing/Light.hlsli"

RaytracingAccelerationStructure Scene : register(t0, space0);

StructuredBuffer<Light> Lights : register(t1, space0);
ByteAddressBuffer InstanceMap : register(t2, space0);

StructuredBuffer<Vertex> Vertices[] : register(t0, space1);
StructuredBuffer<uint3> Indices[] : register(t0, space2);

static const float3 light = float3(0, 200, 0);

void HitCube(inout Payload payload, float2 uv);
void HitMirror(inout Payload payload, float2 uv);
void HitFloor(inout Payload payload, float2 uv);

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    uint instanceID = InstanceID();

    uint meshID = InstanceMap.Load(instanceID * 4);
    
    StructuredBuffer<Vertex> meshVertices = Vertices[meshID];
    StructuredBuffer<uint3> meshIndices = Indices[meshID];
    
    uint3 triIndices = meshIndices[PrimitiveIndex()];
    
    Vertex v0 = meshVertices[triIndices.x];
    Vertex v1 = meshVertices[triIndices.y];
    Vertex v2 = meshVertices[triIndices.z];

    float2 bary = attribs.barycentrics;
    float u = bary.x;
    float v = bary.y;
    float w = 1.0 - u - v;    
    
    float3 normal = normalize(v0.Normal.unpack().xyz * w + v1.Normal.unpack().xyz * u + v2.Normal.unpack().xyz * v);
    
    HitCube(payload, bary);
    
    /*switch (InstanceID())
    {
        case 0: HitCube(payload, uv); break;
        case 1: HitMirror(payload, uv); break;
        case 2: HitFloor(payload, uv); break;

        default: payload.color = float3(1, 0, 1); break;
    }*/
}

void HitCube(inout Payload payload, float2 uv)
{
    uint tri = PrimitiveIndex();

    tri /= 2;
    float3 normal = (tri.xxx % 3 == uint3(0, 1, 2)) * (tri < 3 ? -1 : 1);

    float3 worldNormal = normalize(mul(normal, (float3x3)ObjectToWorld4x3()));

    float3 color = abs(normal) / 3 + 0.5;
    if (uv.x < 0.03 || uv.y < 0.03)
        color = 0.25.rrr;

    color *= saturate(dot(worldNormal, normalize(light))) + 0.33;
    payload.color = color;
}

void HitMirror(inout Payload payload, float2 uv)
{
    if (!payload.allowReflection)
        return;

    float3 pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 normal = normalize(mul(float3(0, 1, 0), (float3x3)ObjectToWorld4x3()));
    float3 reflected = reflect(normalize(WorldRayDirection()), normal);

    RayDesc mirrorRay;
    mirrorRay.Origin = pos;
    mirrorRay.Direction = reflected;
    mirrorRay.TMin = 0.001;
    mirrorRay.TMax = 1000;

    payload.allowReflection = false;
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, mirrorRay, payload);

}

void HitFloor(inout Payload payload, float2 uv)
{
    float3 pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    bool2 pattern = frac(pos.xz) > 0.5;
    payload.color = (pattern.x ^ pattern.y ? 0.6 : 0.4).rrr;

    RayDesc shadowRay;
    shadowRay.Origin = pos;
    shadowRay.Direction = light - pos;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = 1;

    Payload shadow;
    shadow.allowReflection = false;
    shadow.missed = false;
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, shadowRay, shadow);

    if (!shadow.missed)
        payload.color /= 2;
}

