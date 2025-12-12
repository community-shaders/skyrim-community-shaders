#ifndef COMMONRT_HLSI
#define COMMONRT_HLSI

#include "Common/Game.hlsli"
#include "Common/Math.hlsli"

#include "Raytracing/Includes/Types.hlsli"

#ifndef MAX_DEPTH
    #ifdef PATH_TRACING
#define MAX_DEPTH (2)
    #else
#define MAX_DEPTH (1)
    #endif
#endif

#ifndef SAMPLES
#define SAMPLES (1)
#endif

#define SHADOW_MAX_DEPTH (1)

#define DIFFUSE_RAY_HITGROUP_IDX 0
#define DIFFUSE_RAY_MISS_IDX 0

#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1

#define SPECULAR_RAY_HITGROUP_IDX 2
#define SPECULAR_RAY_MISS_IDX 2

#define DEFAULT_METALNESS 0.0
#define DEFAULT_SPECULAR float3(0.5, 0.5, 0.5)
#define DEFAULT_SPECULARF0 (0.04)
#define DEFAULT_ROUGHNESS 0.5

#define MIN_DIELECTRICS_F0 0.04f

#define MIN_ROUGHNESS 0.04f
#define MAX_ROUGHNESS 1.0f

#define GN_OFFSET (0.1f)

uint InitRandomSeed(uint2 coord, uint2 size, uint frameCount)
{
    return coord.x + coord.y * size.x + frameCount * 719393;
}

uint PCGHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random(inout uint seed)
{
    seed = PCGHash(seed);
    return float(seed) / 4294967296.0; // Divide by 2^32
}

void CreateOrthonormalBasis(in float3 normal, out float3 tangent, out float3 bitangent)
{
    float sign = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    
    tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

float3 CosineSampleHemisphere(inout uint seed)
{
    float u1 = Random(seed);
    float u2 = Random(seed);

    float r = sqrt(u1);
    float theta = 2.0 * Math::PI * u2;

    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(1.0 - u1);

    return float3(x, y, z);
}

float3 TangentSampleScaled(inout uint randomSeed, float roughness)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);

    float phi = 2.0 * Math::PI * r1;
    
    float alpha = roughness * roughness;
    float cosTheta = sqrt(max(0.0f, (1.0 - r2) / ((alpha - 1.0) * r2 + 1)));
    float sinTheta = sqrt(max(0.0f, 1.0 - cosTheta * cosTheta));

    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

float3 GGXSample(inout uint randomSeed, float2 alpha)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);
 
    float theta = atan(alpha.y * sqrt(r1) / sqrt(max(1e-6f, 1.0f - r1))); // Walter, Formula (35).
    float phi = 2.0f * Math::PI * r2; // Walter, Formula (36).

    float sinTheta = sin(theta);
    float cosTheta = cos(theta);

    // Heitz, Formula (77)
    float x = cos(phi) * sinTheta * (alpha.x / alpha.y);
    float y = sin(phi) * sinTheta;
    float z = cosTheta;

    return normalize(float3(x, y, z));
}

float3 SampleGGX(const float roughness, inout uint randomSeed)
{
    const float r1 = Random(randomSeed);
    const float r2 = Random(randomSeed);
    
    const float a = max(0.001f, roughness);
    const float phi = Math::PI * r1;
    const float cos_theta = sqrt((1.0f - r2) / (1.0f + (a*a - 1.0f) * r2));
    const float sin_theta = sqrt(1.0f - cos_theta * cos_theta);

    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

float3 SampleGGXVNDF_Direct(float3 V_tan, inout uint randomSeed, float roughness)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);

    float phi = 2.0 * Math::PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (roughness*roughness - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H_tan = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // L_Tan
    return normalize(2.0 * dot(V_tan, H_tan) * H_tan - V_tan);
}

float3 TangentToWorld(float3 normal, float3 tangentSample)
{   
    float3 tangent;
    float3 bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);

    return tangent * tangentSample.x +
           bitangent * tangentSample.y +
           normal * tangentSample.z;
}

float D_GGX(float NoH, float roughness) {
    float a = NoH * roughness;
    float k = roughness / (1.0 - NoH * NoH + a * a);
    return k * k * (1.0 / Math::PI);
}

float3 F0(float3 albedo, float metalness)
{
    return lerp(DEFAULT_SPECULARF0.xxx, albedo, metalness);
}

float3 F_Schlick(float u, float3 f0) {
    float f = pow(1.0 - u, 5.0);
    return f + f0 * (1.0 - f);
}

float V_SmithGGXCorrelatedFast(float NoV, float NoL, float roughness) {
    float a = roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert() {
    return 1.0 / Math::PI;
}

float DiffuseProbability(float roughness, float metalness, float3 f0, float NoV)
{
    if (metalness >= 1.0f) return 0.0f;

    float3 F = F_Schlick(NoV, f0);
    
    float specularEnergy = (F.r + F.g + F.b) / 3.0f;
    
    float diffuseEnergy = (1.0f - specularEnergy) * (1.0 - 0.2 * roughness);
    
    return clamp(diffuseEnergy, 0.05f, 0.95f);
}

float SpecularAO(float ao)
{
    return ao * 0.25f + 0.75f;
}

#endif // COMMONRT_HLSI