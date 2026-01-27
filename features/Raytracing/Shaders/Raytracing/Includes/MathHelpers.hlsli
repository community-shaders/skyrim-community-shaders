#ifndef __MATH_HELPERS_HLSLI__
#define __MATH_HELPERS_HLSLI__

#include "Raytracing/Includes/MathConstants.hlsli"

inline float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

inline float Average(float3 rgb)
{
    return (rgb.x+rgb.y+rgb.z) / 3.0;
}

/** Uniform sampling of the unit disk using polar coordinates.
    \param[in] u Uniform random number in [0,1)^2.
    \return Sampled point on the unit disk.
*/
float2 sample_disk(float2 u)
{
    float2 p;
    float r = sqrt(u.x);
    float phi = K_2PI * u.y;
    p.x = r * cos(phi);
    p.y = r * sin(phi);
    return p;
}

/** Uniform sampling of direction within a cone
    \param[in] u Uniform random number in [0,1)^2.
    \param[in] cosTheta Cosine of the cone half-angle
    \return Sampled direction within the cone with (0,0,1) axis
*/
float3 sample_cone(float2 u, float cosTheta)
{
    float z = u.x * (1.f - cosTheta) + cosTheta;
    float r = sqrt(1.f - z*z);
    float phi = K_2PI * u.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

/** Uniform sampling of the unit sphere using spherical coordinates.
    \param[in] u Uniform random numbers in [0,1)^2.
    \return Sampled point on the unit sphere.
*/
float3 sample_sphere(float2 u)
{
    float phi = K_2PI * u.y;
    float cosTheta = 1.0f - 2.0f * u.x;
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

/** Uniform sampling of the unit hemisphere using sphere sampling.
    \param[in] u Uniform random numbers in [0,1)^2.
    \return Sampled point on the unit hemisphere.
*/
float3 sample_hemisphere(float2 u)
{
    float3 w = sample_sphere(u);
    w.z = abs(w.z);
    return w;
}

/** Uniform sampling of the unit disk using Shirley's concentric mapping.
    \param[in] u Uniform random numbers in [0,1)^2.
    \return Sampled point on the unit disk.
*/
float2 sample_disk_concentric(float2 u)
{
    u = 2.f * u - 1.f;
    if (u.x == 0.f && u.y == 0.f) return u;
    float phi, r;
    if (abs(u.x) > abs(u.y))
    {
        r = u.x;
        phi = (u.y / u.x) * K_PI_4;
    }
    else
    {
        r = u.y;
        phi = K_PI_2 - (u.x / u.y) * K_PI_4;
    }
    return r * float2(cos(phi), sin(phi));
}

/** Cosine-weighted sampling of the hemisphere using Shirley's concentric mapping.
    \param[in] u Uniform random numbers in [0,1)^2.
    \param[out] pdf Probability density of the sampled direction (= cos(theta)/pi).
    \return Sampled direction in the local frame (+z axis up).
*/
float3 sample_cosine_hemisphere_concentric(float2 u, out float pdf)
{
    float2 d = sample_disk_concentric(u);
    float z = sqrt(max(0.f, 1.f - dot(d, d)));
    pdf = z * K_1_PI;
    return float3(d, z);
}

#endif