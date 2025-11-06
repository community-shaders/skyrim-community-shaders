#ifndef COMMON_HLSI
#define COMMON_HLSI

#include "RaytracedGI/Raytracing/Types.hlsli"

#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))
#define MAX_DEPTH 2

uint InitRandomSeed(uint2 coord, uint2 size, uint frameCount)
{
    // Simple hash function
    uint seed = coord.x + coord.y * size.x + frameCount * 719393;
    return seed;
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

// Generate random direction in hemisphere around normal
float3 SampleHemisphere(float3 normal, inout uint randomSeed)
{
    // Generate two random numbers
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);
    
    // Cosine-weighted hemisphere sampling (Malley's method)
    float phi = 2.0 * 3.14159265359 * r1;
    float cosTheta = sqrt(r2);
    float sinTheta = sqrt(1.0 - r2);
    
    // Convert to Cartesian coordinates in tangent space
    float3 tangentSample = float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
    
    // Build tangent space basis from normal
    float3 tangent = abs(normal.z) < 0.999 
        ? normalize(cross(normal, float3(0, 0, 1))) 
        : normalize(cross(normal, float3(1, 0, 0)));
    float3 bitangent = cross(normal, tangent);
    
    // Transform from tangent space to world space
    return normalize(
        tangent * tangentSample.x +
        bitangent * tangentSample.y +
        normal * tangentSample.z
    );
}

#endif