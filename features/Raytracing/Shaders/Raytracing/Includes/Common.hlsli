#ifndef COMMON_HLSL
#define COMMON_HLSL

#define DEPTH_SCALE (0.99920h)
#define FP_Z (16.5f)
#define SKY_Z (0.9999f)
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

void NormalMap(float3 normalMap, float3 geomNormalWS, float3 geomTangentWS, float3 geomBitangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
	float tangentSign = (dot(cross(geomNormalWS, geomTangentWS), geomTangentWS) < 0.0f) ? -1.0f : 1.0f;
	
	normalMap = normalMap * 2.0f - 1.0f;
	
    normalWS = normalize(
		normalMap.x * geomTangentWS +
		normalMap.y * geomBitangentWS +
		normalMap.z * geomNormalWS
	);

    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS)); 
    bitangentWS = cross(normalWS, tangentWS) * tangentSign;  
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

float2 Get2D(uint seed)
{
	return (SamplerCore(seed).xy) * 5.96046447754e-08;
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

#endif