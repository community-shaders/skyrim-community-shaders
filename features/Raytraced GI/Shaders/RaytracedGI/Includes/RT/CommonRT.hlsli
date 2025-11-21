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

#define DEFAULT_METALNESS 0.0
#define DEFAULT_SPECULAR float3(0.5, 0.5, 0.5)
#define DEFAULT_SPECULARF0 float3(0.04, 0.04, 0.04)
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

float3 TangentToWorld(float3 normal, float3 tangentSample)
{   
    float3 tangent;
    float3 bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);

    return tangent * tangentSample.x +
           bitangent * tangentSample.y +
           normal * tangentSample.z;
}

float D_GGX_Ani(float2 alpha, float3 H)
{
    float denom = (H.x*H.x) / (alpha.x*alpha.x) + (H.y*H.y) / (alpha.y*alpha.y) + H.z*H.z;
    return 1.0 / max(Math::PI * alpha.x * alpha.y * denom * denom, EPSILON_DIVISION);
}

float G1_Smith_Ani(float2 alpha, float3 V)
{
    float a = sqrt((V.x*V.x) / (alpha.x*alpha.x) + (V.y*V.y) / (alpha.y*alpha.y) + V.z*V.z);
    return 2.0 / (1.0 + sqrt(1.0 + a*a));
}

float Vis_Smith_Ani(float2 alpha, float3 V, float3 L)
{
    return G1_Smith_Ani(alpha, V) * G1_Smith_Ani(alpha, L);
}

float D_GGX(float roughness, float NdotH)
{
    float a = roughness * roughness;
    float NdotH2 = NdotH * NdotH;
    float a2 = a * a;
    float d = NdotH2 * (a2 - 1.0) + 1.0;
    return (a2 / (Math::PI * d * d));
}

float Vis_Smith(float roughness, float NdotV, float NdotL)
{
    float a = roughness * roughness;
    float a2 = a * a;   
    float Vis_SmithV = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    float Vis_SmithL = NdotL + sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
    return max(Vis_SmithV * Vis_SmithL, EPSILON_DIVISION);
}

float3 F_Schlick(float3 specularColor, float VdotH)
{
	float Fc = pow(1 - VdotH, 5);
	return Fc + (1 - Fc) * specularColor;
}

float3 ComputeF0Aniso(float3 baseColor, float metallic, float anisotropy)
{
    // Dielectric F0
    float3 dielectricF0 = float3(0.04, 0.04, 0.04);

    // Metal F0 along X and Y axes, scaled by anisotropy
    float3 F0x = lerp(dielectricF0, baseColor, metallic * (1.0 - anisotropy));
    float3 F0y = lerp(dielectricF0, baseColor, metallic * (1.0 + anisotropy));

    return float3(F0x.r, F0y.g, max(F0x.b, F0y.b)); // pack for shader use
}

float3 F_SchlickAniso(float3 F0x, float3 F0y, float VdotH, float tangentH, float bitangentH)
{
    float Fc = pow(1.0f - VdotH, 5);

    // interpolate F0 along tangent/bitangent axes
    float3 F0 = F0x * tangentH*tangentH + F0y * bitangentH*bitangentH;

    return F0 + (1.0f - F0) * Fc;
}

float3 F_Schlick2(float3 F0, float VdotH)
{
    float Fc = pow(1.0 - VdotH, 5.0);
    return F0 + (1.0 - F0) * Fc;
}

float3 F_Schlick3(float3 specularColor, float VdotH)
{
    float Fc = pow(1.0 - VdotH, 5.0);
    return (1- Fc) * specularColor + Fc;
}

float3 F_Schlick(float3 F0, float3 F90, float VdotH)
{
    float Fc = pow(1 - VdotH, 5);
    return F0 + (F90 - F0) * Fc;
}

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

float DiffuseProbability(float3 albedo, float3 specular, float metalness, float roughness, float NdotV)
{
    if (metalness == 1.0f && roughness == 0.0f) return 0.0f;

    float3 F0 = lerp(float3(0.04,0.04,0.04), specular, metalness);
    float3 F = F_Schlick(F0, float3(1.0f, 1.0f, 1.0f), NdotV);
    
    float specularEnergy = Luminance(F) * (1.0f - 0.5f * roughness);
    
    return clamp(1.0f - specularEnergy, 0.1f, 0.9f);
}

#endif // COMMONRT_HLSI