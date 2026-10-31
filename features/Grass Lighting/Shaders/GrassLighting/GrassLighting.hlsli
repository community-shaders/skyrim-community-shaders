#include "Common/PBRMath.hlsli"

namespace GrassLighting
{
	void GetSubsurfaceAlbedos(float3 albedo, float amount, float balance,
		out float3 reflectionAlbedo, out float3 frontTransmissionAlbedo, out float3 backTransmissionAlbedo)
	{
		float reflectionWeight;
		PBR::GetThinSurfaceSubsurfaceAlbedos(albedo, albedo, 1.0 - 0.5 * saturate(amount), balance,
			reflectionWeight, frontTransmissionAlbedo, backTransmissionAlbedo);
		reflectionAlbedo = albedo * reflectionWeight;
	}

	float3 GetLightSpecularInput(float3 L, float3 V, float3 N, float3 lightColor, float shininess)
	{
		float3 H = normalize(V + L);
		// log2(0) times a zero shininess is NaN
		float HdotN = max(saturate(dot(H, N)), 1e-6);

		float lightColorMultiplier = exp2(shininess * log2(HdotN));
		return lightColor * lightColorMultiplier.xxx;
	}

	float GetCanopyVisibility(float canopyHeight)
	{
		return 1.0 - SharedData::grassLightingSettings.CanopyOcclusion * (1.0 - saturate(canopyHeight));
	}

	// Bend within the normal's tangent plane to avoid hemisphere flips and cancellation.
	float3 GetClumpBlendedNormal(float3 bladeNormal, float3 clumpNormal)
	{
		float3 bend = clumpNormal - bladeNormal * dot(bladeNormal, clumpNormal);
		return normalize(bladeNormal + bend * saturate(SharedData::grassLightingSettings.ClumpShading));
	}

	// Clump occlusion must not depend on which side of a blade faces the light.
	float GetClumpLightVisibility(float3 clumpNormal, float3 L)
	{
		return lerp(1.0, saturate(dot(clumpNormal, L) * 0.5 + 0.5), SharedData::grassLightingSettings.ClumpShading);
	}

	float3 TransformNormal(float3 normal)
	{
		return normal * 2 + -1.0.xxx;
	}

	// http://www.thetenthplanet.de/archives/1180
	float3x3 CalculateTBN(float3 N, float3 p, float2 uv)
	{
		// get edge vectors of the pixel triangle
		float3 dp1 = ddx_coarse(p);
		float3 dp2 = ddy_coarse(p);
		float2 duv1 = ddx_coarse(uv);
		float2 duv2 = ddy_coarse(uv);

		// solve the linear system
		float3 dp2perp = cross(dp2, N);
		float3 dp1perp = cross(N, dp1);
		float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

		// construct a scale-invariant frame
		float invmax = rsqrt(max(dot(T, T), dot(B, B)));
		return float3x3(T * invmax, B * invmax, N);
	}
}
