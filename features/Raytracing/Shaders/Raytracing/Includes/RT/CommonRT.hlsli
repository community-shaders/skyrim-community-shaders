#ifndef COMMONRT_HLSI
#define COMMONRT_HLSI

#include "Common/Game.hlsli"
#include "Common/Math.hlsli"

#include "Raytracing/Includes/Types.hlsli"

#define MAX_DEPTH 1
#define SHADOW_MAX_DEPTH 1

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
    //float sign = copysign(1.0f, normal.z);
    float sign = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    
    tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

float3 TangentSample(inout uint randomSeed)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);
    
    float phi = 2.0 * Math::PI * r1;
    
    float cosTheta = sqrt(1.0 - r2);
    float sinTheta = sqrt(r2);

    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
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

float3 SampleGGXVNDF_Direct(float3 V_tan, inout uint randomSeed, float roughness)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);

    float phi = 2.0 * Math::PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (roughness*roughness - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H_tan = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    float3 L_tan = normalize(2.0 * dot(V_tan, H_tan) * H_tan - V_tan);

    return L_tan;
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
    return 0.16 * DEFAULT_SPECULARF0 * (1.0 - metalness) + albedo * metalness; 
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

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

float DiffuseProbability(float3 albedo, float3 specular, float metalness, float roughness, float NdotV)
{
    if (metalness == 1.0f && roughness == 0.0f) return 0.0f;

    float3 f0 = 0.16 * DEFAULT_SPECULARF0 * (1.0 - metalness) + albedo * metalness;        
    float3 F = F_Schlick(NdotV, f0); // LoH
    
    float specularEnergy = Luminance(F) * (1.0f - 0.5f * roughness);
    
    return clamp(1.0f - specularEnergy, 0.1f, 0.9f);
}

#endif // COMMONRT_HLSI