#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

#define Pow2(x) ((x) * (x))

Texture2D<unorm float3> NormalRoughnessTexture : register(t2);

SamplerState LinearSampler : register(s0);

// Brian Karis, Epic Games "Real Shading in Unreal Engine 4"
float4 ImportanceSampleGGX(float2 E, float a2)
{
	float Phi = 2 * Math::PI * E.x;
	float CosTheta = sqrt( (1 - E.y) / ( 1 + (a2 - 1) * E.y ) );
	float SinTheta = sqrt( 1 - CosTheta * CosTheta );

	float3 H;
	H.x = SinTheta * cos( Phi );
	H.y = SinTheta * sin( Phi );
	H.z = CosTheta;
	
	float d = ( CosTheta * a2 - CosTheta ) * CosTheta + 1;
	float D = a2 / ( Math::PI*d*d );
	float PDF = D * CosTheta;

	return float4( H, PDF );
}

float VisibleGGXPDF_aniso(float3 V, float3 H, float2 Alpha, bool bLimitVDNFToReflection = true)
{
	float NoV = V.z;
	float NoH = H.z;
	float VoH = dot(V, H);
	float a2 = Alpha.x * Alpha.y;
	float3 Hs = float3(Alpha.y * H.x, Alpha.x * H.y, a2 * NoH);
	float S = dot(Hs, Hs);
	float D = (1.0f / Math::PI) * a2 * Pow2(a2 / S);
	float LenV = length(float3(V.x * Alpha.x, V.y * Alpha.y, NoV));
	float k = 1.0;
	if (bLimitVDNFToReflection)
	{
		float a = saturate(min(Alpha.x, Alpha.y));
		float s = 1.0f + length(V.xy);
		float ka2 = a * a, s2 = s * s;
		k = (s2 - ka2 * s2) / (s2 + ka2 * V.z * V.z); // Eq. 5
	}
	float Pdf = (2 * D * VoH) / (k * NoV + LenV);
	return Pdf;
}

// PDF = G_SmithV * VoH * D / NoV / (4 * VoH)
// PDF = G_SmithV * D / (4 * NoV)
float4 ImportanceSampleVisibleGGX(float2 E, float2 Alpha, float3 V, bool bLimitVDNFToReflection = true)
{
	// stretch
	float3 Vh = normalize(float3(Alpha * V.xy, V.z));

	// "Sampling Visible GGX Normals with Spherical Caps"
	// Jonathan Dupuy & Anis Benyoub - High Performance Graphics 2023
	float Phi = (2 * Math::PI) * E.x;
	float k = 1.0;
	if (bLimitVDNFToReflection)
	{
		// If we know we will be reflecting the view vector around the sampled micronormal, we can
		// tweak the range a bit more to eliminate some of the vectors that will point below the horizon
		float a = saturate(min(Alpha.x, Alpha.y));
		float s = 1.0 + length(V.xy);
		float a2 = a * a, s2 = s * s;
		k = (s2 - a2 * s2) / (s2 + a2 * V.z * V.z);
	}
	float Z = lerp(1.0, -k * Vh.z, E.y);
	float SinTheta = sqrt(saturate(1 - Z * Z));
	float X = SinTheta * cos(Phi);
	float Y = SinTheta * sin(Phi);
	float3 H = float3(X, Y, Z) + Vh;

	// unstretch
	H = normalize(float3(Alpha * H.xy, max(0.0, H.z)));

	return float4(H, VisibleGGXPDF_aniso(V, H, Alpha));
}

void GetNormalRoughness(uint2 dtid, out float3 normal, out float roughness)
{
    float3 normalGlossiness = NormalRoughnessTexture[dtid];
    // Normal is in view space
    normal = GBuffer::DecodeNormal(normalGlossiness.xy);
    roughness = 1.0f - normalGlossiness.z;
}

// [ Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ]
float3x3 GetTangentBasis( float3 TangentZ )
{
	const float Sign = TangentZ.z >= 0 ? 1 : -1;
	const float a = -rcp( Sign + TangentZ.z );
	const float b = TangentZ.x * TangentZ.y * a;
	
	float3 TangentX = { 1 + Sign * a * Pow2( TangentZ.x ), Sign * b, -Sign * TangentZ.x };
	float3 TangentY = { b,  Sign + a * Pow2( TangentZ.y ), -TangentZ.y };

	return float3x3( TangentX, TangentY, TangentZ );
}

float2 Hammersley16( uint Index, uint NumSamples, uint2 Random )
{
	float E1 = frac( (float)Index / NumSamples + float( Random.x ) * (1.0 / 65536.0) );
	float E2 = float( ( reversebits(Index) >> 16 ) ^ Random.y ) * (1.0 / 65536.0);
	return float2( E1, E2 );
}

static const int2 kStackowiakSampleSet4[15] = { int2(0, 1), int2(-2, 1), int2(2, -3), int2(-3, 0), int2(1, 2), int2(-1, -2), int2(3, 0), int2(-3, 3), int2(0, -3), int2(-1, -1), int2(2, 1), int2(-2, -2), int2(1, 0), int2(0, 2), int2(3, -1) };

float2 GetMotionVector(float sceneDepth, float2 screenUV, float4x4 matrix_LastViewProj, float4x4 matrix_ViewProj)
{
    float4 positionWS = float4(2 * float2(screenUV.x, -screenUV.y + 1) - 1, sceneDepth, 1);

    float4 curClipPos = mul(matrix_ViewProj, positionWS);
    float4 lastClipPos = mul(matrix_LastViewProj, positionWS);

    float2 CurNDC = curClipPos.xy / curClipPos.w;
    float2 LastNDC = lastClipPos.xy / lastClipPos.w;

    float2 CurUV = CurNDC.xy * float2(0.5f, -0.5f) + 0.5;
    float2 LastUV = LastNDC.xy * float2(0.5f, -0.5f) + 0.5;
    
    return CurUV - LastUV;
}