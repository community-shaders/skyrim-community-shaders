#ifndef COMMONRT_HLSI
#define COMMONRT_HLSI

#include "Common/Game.hlsli"
#include "Common/Math.hlsli"
#include "Common/Color.hlsli"

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

#define MIN_DIFFUSE_SHADOW (0.001f)

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

float calcLuminance(float3 color)
{
    return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
}

float3 SampleGGX_VNDF(float3 Ve, float alpha, inout uint seed)
{
    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float r1 = Random(seed);
    float r2 = Random(seed);   
    
    float r = sqrt(r1);
    float phi = 2.0 * Math::PI * r2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - (t1 * t1) - (t2 * t2))) * Vh;

    // Tangent space H
    return float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z));
}

inline float square(float value)
{
    return value * value;
}

float ImportanceSampleGGX_VNDF_PDF(float alpha, float3 N, float3 V, float3 L)
{
    float3 H = normalize(L + V);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    float D = square(alpha) / (Math::PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));
    return (VoH > 0.0) ? D / (4.0 * VoH) : 0.0;
}

float Schlick_Fresnel(float F0, float VdotH)
{
    return F0 + (1 - F0) * pow(max(1 - VdotH, 0), 5);
}

float3 Schlick_Fresnel(float3 F0, float VdotH)
{
    return F0 + (1 - F0) * pow(max(1 - VdotH, 0), 5);
}

float G1_Smith(float alpha, float NdotL)
{
    return 2.0 * NdotL / (NdotL + sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotL)));
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
    return saturate(lerp(DEFAULT_SPECULARF0.xxx, albedo, metalness));
}

float3 F_Schlick(float u, float3 f0) {
    float f = pow(1.0 - u, 5.0);
    return f + f0 * (1.0 - f);
}

float V_SmithGGXCorrelatedFast(float NoV, float NoL, float a) {
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

float3 DiffuseAO(float3 diffuseColor, float ao)
{
	return Color::MultiBounceAO(diffuseColor, ao);
}

float3 SpecularAO(float NdotV, float roughness, float ao, float3 f0)
{
	float specularAO = Color::SpecularAOLagarde(NdotV, ao, roughness);
	return Color::MultiBounceAO(f0, specularAO);
}

// Horizon specular occlusion
// https://marmosetco.tumblr.com/post/81245981087
float Horizon(float3 V, float3 N, float3 VN)
{
	float3 R = reflect(-V, N);
	float horizon = min(1.0 + dot(R, VN), 1.0);
    
	return  horizon * horizon;
}

#endif // COMMONRT_HLSI