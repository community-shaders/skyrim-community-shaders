#ifndef __SHADING_HLSLI__
#define __SHADING_HLSLI__

#include "Common/Math.hlsli"

// [Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"]
float3 MultiBounceAO(float3 baseColor, float ao)
{
	float3 a = 2.0404 * baseColor - 0.3324;
	float3 b = -4.7951 * baseColor + 0.6417;
	float3 c = 2.7552 * baseColor + 0.6903;
	return max(ao, ((ao * a + b) * ao + c) * ao);
}

// [Lagarde et al. 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
float SpecularAOLagarde(float NdotV, float ao, float roughness)
{
	return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

float SpecularOcclusion(float NdotV, float alpha, float occlusion)
{
	return saturate(pow(abs(NdotV + occlusion), alpha) - 1.0 + occlusion);
}

// a contact shadow approximation, totally not physically correct; a riff on "Chan 2018, "Material Advances in Call of Duty: WWII" and "The Technical Art of Uncharted 4" http://advances.realtimerendering.com/other/2016/naughty_dog/NaughtyDog_TechArt_Final.pdf (microshadowing)"
float ApproximateDirectOcculusion(float aoVisibility, float NdotL)
{
	float aperture = rsqrt(1.0000001 - aoVisibility);
	NdotL += 0.1;  // when using bent normals, avoids overshadowing - bent normals are just approximation anyhow
	return saturate(NdotL * aperture);
}

// https://blog.selfshadow.com/publications/blending-in-detail/
// geometric normal s, a base normal t and a secondary (or detail) normal u
float3 ReorientNormal(float3 u, float3 t, float3 s)
{
	// Build the shortest-arc quaternion
	float4 q = float4(cross(s, t), dot(s, t) + 1) / sqrt(2 * (dot(s, t) + 1));

	// Rotate the normal
	return u * (q.w * q.w - dot(q.xyz, q.xyz)) + 2 * q.xyz * dot(q.xyz, u) + 2 * q.w * cross(q.xyz, u);
}

// for when s = (0,0,1)
float3 ReorientNormal(float3 n1, float3 n2)
{
	n1 += float3(0, 0, 1);
	n2 *= float3(-1, -1, 1);

	return n1 * dot(n1, n2) / n1.z - n2;
}

float3x3 ReconstructTBN(float3 worldPos, float3 worldNormal, float2 uv)
{
	float3 dFdx = ddx(worldPos);
	float3 dFdy = ddy(worldPos);
	float2 dUVdx = ddx(uv);
	float2 dUVdy = ddy(uv);
	float3 tangent = normalize(dFdx * dUVdy.y - dFdy * dUVdx.y);
	float3 bitangent = normalize(dFdy * dUVdx.x - dFdx * dUVdy.x);
	tangent = normalize(tangent - worldNormal * dot(worldNormal, tangent));
	bitangent = normalize(bitangent - worldNormal * dot(worldNormal, bitangent));

	return float3x3(tangent, bitangent, normalize(worldNormal));
}

float3 CalculateNormalFromHeight(float height, float heightScale, float2 uv)
{
	float dHdx = ddx(height);
	float dHdy = ddy(height);
	float2 dUVdx = ddx(uv);
	float2 dUVdy = ddy(uv);

	float det = dUVdx.x * dUVdy.y - dUVdx.y * dUVdy.x;
	if (det < EPSILON_DIVISION) {
		return float3(0, 0, 1);  // Avoid division by zero
	}

	float dHdx_Tex = (dHdx * dUVdy.y - dHdy * dUVdx.y) / det;
	float dHdy_Tex = (dHdy * dUVdx.x - dHdx * dUVdy.x) / det;
	float3 normal = float3(-dHdx_Tex, -dHdy_Tex, 0);
	return normal * heightScale + float3(0, 0, 1);
}

#endif  // __SHADING_HLSLI__