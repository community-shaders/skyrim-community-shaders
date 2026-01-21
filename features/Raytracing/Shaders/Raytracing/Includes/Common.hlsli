#ifndef COMMON_HLSL
#define COMMON_HLSL

#include "Common/Game.hlsli"

#define DEPTH_SCALE (0.99920h)

#define FP_Z (0.001f)
#define SKY_Z (0.9999f)

#define FP_VIEW_Z (16.5f)

#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))

float ScreenToViewDepth(const float screenDepth, float4 cameraData)
{
	return (cameraData.w / (-screenDepth * cameraData.z + cameraData.x));
}

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth, const float4 ndcToView)
{
	float3 ret;
	ret.xy = (ndcToView.xy * screenPos.xy + ndcToView.zw) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

float Remap(float x, float min, float max)
{
    return clamp(min + saturate(x) * (max - min), min, max);
}

inline float Square(float value)
{
    return value * value;
}

half3 DecodeNormal(half2 f)
{
	f = f * 2.0 - 1.0;
	// https://twitter.com/Stubbesaurus/status/937994790553227264
	half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	half t = saturate(-n.z);
	#if !defined(DX11)
	n.xy += select(n.xy >= 0.0, -t, t);
	#else
	n.xy += n.xy >= 0.0 ? -t : t;
	#endif
	return -normalize(n);
}

void NormalMap(float3 normalMap, float handedness, float3 geomNormalWS, float3 geomTangentWS, float3 geomBitangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
    normalWS = normalize(
		normalMap.x * geomTangentWS +
		normalMap.y * geomBitangentWS +
		normalMap.z * geomNormalWS
	);

    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS));
    bitangentWS = cross(normalWS, tangentWS) * handedness;
}

void ModelSpaceNormalMap(float3 normalMap, float handedness, float3 geomNormalWS, float3 geomTangentWS, float3 geomBitangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
	float3x3 tbn = float3x3(geomTangentWS, geomBitangentWS, geomNormalWS);

	normalWS = normalize(mul(tbn, normalMap.xyz));
    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS));
    bitangentWS = cross(normalWS, tangentWS) * handedness;
}

void ModelSpaceNormalMap(float3 normalMap, float handedness, float3x3 objectToWorld3x3, float3 geomTangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
	normalWS = normalize(mul(objectToWorld3x3, normalMap.xzy));
    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS));
    bitangentWS = cross(normalWS, tangentWS) * handedness;
}

uint StrongIntegerHash(uint x)
{
	// From https://github.com/skeeto/hash-prospector
	// Current best hash in this form: https://github.com/skeeto/hash-prospector/issues/19#issuecomment-1105792898
	// bias = 0.10734781817103507
	x ^= x >> 16;
	x *= 0x21f0aaad;
	x ^= x >> 15;
	x *= 0xf35a2d97;
	x ^= x >> 15;
	return x;
}

uint4 SamplerCore(inout uint seed)
{
	uint4 result = uint4(StrongIntegerHash(seed + 0),
						 StrongIntegerHash(seed + 1),
						 StrongIntegerHash(seed + 2),
						 StrongIntegerHash(seed + 3));
	seed += 4;
	return result;
}

float2 Get2D(inout uint seed)
{
	return (SamplerCore(seed).xy) * 5.96046447754e-08;
}

// I keep it here because it is also used by DX11 to make the Diffuse Albedo texture from 'True Albedo'
void UnpackMAO(float packed, out float metalness, out float ao)
{
    uint metalnessAO = packed * 65535.0;

    metalness = saturate((metalnessAO & 0xFF) / 255.0f);
    ao = saturate(((metalnessAO >> 8) & 0xFF) / 255.0f);
}

float ShadowTerminatorTerm(float3 L, float3 N, float3 Ns)
{
	// Disney terminator softening:
	// "Taming the Shadow Terminator"
	// Matt Jen-Yuan Chiang, Yining Karl Li, and Brent Burley
	// SIGGRAPH 2019 Talks
	// https://www.yiningkarlli.com/projects/shadowterminator.html
	const float NoL = saturate(dot(N, L));
	const float NgoL = saturate(dot(Ns, L));
	const float NgoN = saturate(dot(Ns, N));
	const float G = saturate(NgoL / (NoL * NgoN + 1e-6));
	return G + G * (G - G * G); // smooth
}

float F0toIOR(float3 F0)
{
	float f0 = max(max(F0.r, F0.g), F0.b);
	return (1.0 + sqrt(f0)) / (1.0 - sqrt(f0));
}

// Compute the cosine of the angle of refraction with respect to the surface
// normal, given the cosine of the angle of incidence with respect to the
// surface normal and the relative index of refraction at the interface
// (IOR of incident medium over that of the refracting medium).  Both angles
// are measured with respect to the same surface normal.  In case of total
// internal reflection, the return value is zero.
float ComputeCosThetaRefracted(float eta, float cosTheta)
{
    float cos2ThetaRefracted = 1 - eta * eta * (1 - cosTheta * cosTheta);
    return -sign(cosTheta) * sqrt(max(cos2ThetaRefracted, 0.0));
}

// Compute surface reflectance using the Fresnel equations given a relative
// index of refraction and cosines of the angles of the incident and refracted
// rays with respect to the surface normal. Both angles are measured with
// respect to the same surface normal.
float FresnelDielectric(float eta, float cosTheta1, float cosTheta2)
{
    float ks = eta * cosTheta1;
    float sqrtRs = (ks + cosTheta2) / (ks - cosTheta2);
    float kp = eta * cosTheta2;
    float sqrtRp = (kp + cosTheta1) / (kp - cosTheta1);
    return 0.5 * (sqrtRs * sqrtRs + sqrtRp * sqrtRp);
}

float FresnelDielectric(float eta, float cosTheta1)
{
    float cosTheta2 = ComputeCosThetaRefracted(eta, cosTheta1);
    return FresnelDielectric(eta, cosTheta1, cosTheta2);
}

#endif