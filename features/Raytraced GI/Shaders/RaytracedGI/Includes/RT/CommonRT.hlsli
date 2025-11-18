#ifndef COMMONRT_HLSI
#define COMMONRT_HLSI

#include "Common/Game.hlsli"
#include "Common/Math.hlsli"

#include "RaytracedGI/Includes/Types.hlsli"

#define MAX_DEPTH 2
#define SHADOW_MAX_DEPTH 1

#define DIFFUSE_RAY_HITGROUP_IDX 0
#define DIFFUSE_RAY_MISS_IDX 0

#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1

#define SPECULAR_RAY_HITGROUP_IDX 2
#define SPECULAR_RAY_MISS_IDX 2

#define DEFAULT_METALIC 0.0
#define DEFAULT_SPECULAR float3(0.04, 0.04, 0.04)
#define DEFAULT_ROUGHNESS 0.5

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

float3 SampleHemisphere(float3 normal, float3 tangentSample)
{   
    float3 tangent;
    float3 bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);

    return tangent * tangentSample.x +
           bitangent * tangentSample.y +
           normal * tangentSample.z;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float ProbabilityToSampleDiffuse(float3 difColor, float3 specColor)
{
	float lumDiffuse = max(0.01f, Luminance(difColor.rgb));
	float lumSpecular = max(0.01f, Luminance(specColor.rgb));
	return lumDiffuse / (lumDiffuse + lumSpecular);
}
#endif // COMMONRT_HLSI