#ifndef COMMONRT_HLSL
#define COMMONRT_HLSL

#include "Common/Game.hlsli"
#include "Common/Math.hlsli"
#include "Common/Color.hlsli"
#include "Common/BRDF.hlsli"

#include "Raytracing/Includes/Types.hlsli"

#ifndef MAX_BOUNCES
#define     MAX_BOUNCES (1)
#endif

#ifndef MAX_SAMPLES
#define     MAX_SAMPLES (1)
#endif

#define SHADOW_MAX_DEPTH (1)

#define DIFFUSE_RAY_HITGROUP_IDX 0
#define DIFFUSE_RAY_MISS_IDX 0

#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1

#define RAY_TMAX (1e10f)
#define SHADOW_RAY_TMAX (1e5f)

#define GN_OFFSET (0.1f)

#define MIN_DIFFUSE_SHADOW (0.001f)
#define MIN_RADIANCE (0.01f)
#define RR_MIN_BOUNCE (3)

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
    float3 up = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    
    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

float3 SampleCosineHemisphere(inout uint seed)
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

float3 SampleCosineHemisphereScaled(inout uint randomSeed, in float scale)
{
    // Generate two uniform random numbers
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);

    // Azimuthal angle
    float phi = 2.0f * Math::PI * r1;

    // Maximum cone angle
    float cosMax = cos(saturate(scale) * Math::PI / 2.0f);

    // Cosine of polar angle within cone
    float cosTheta = lerp(cosMax, 1.0f, sqrt(1.0f - r2)); // cosine-weighted
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    // Convert to Cartesian coordinates
    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
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

#endif // COMMONRT_HLSI