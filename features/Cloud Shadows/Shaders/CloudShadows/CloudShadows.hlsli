#include "Common/Game.hlsli"

namespace CloudShadows
{
	TextureCube<float> CloudShadowsTexture : register(t25);

	// 2 km cloud altitude / 6371 km planet radius converted from metres
	// to game units. The original 1.428e-2 is GAME_UNIT_TO_M (1.428f / 100),
	// so dividing by GAME_UNIT_TO_M is the exact same value. Game.hlsli
	// parenthesizes the macro, so the operator precedence is preserved.
	const static float CloudHeight = (2e3f / GAME_UNIT_TO_M) * 0.25;
	const static float PlanetRadius = (6371e3f / GAME_UNIT_TO_M);
	const static float RcpHPlusR = (1.0 / (CloudHeight + PlanetRadius));

	float3 GetCloudShadowSampleDir(float3 rel_pos, float3 eye_to_sun)
	{
		float r = PlanetRadius;
		float3 p = (rel_pos + float3(0, 0, r)) * RcpHPlusR;
		float dotprod = dot(p, eye_to_sun);
		float lengthsqr = dot(p, p);
		float t = -dotprod + sqrt(dotprod * dotprod - dot(p, p) + 1);
		float3 v = (p + eye_to_sun * t) * (r + CloudHeight) - float3(0, 0, r);
		return v;
	}

	float GetCloudShadowMult(float3 worldPosition, SamplerState textureSampler)
	{
		float3 cloudSampleDir = GetCloudShadowSampleDir(worldPosition, SharedData::DirLightDirection.xyz).xyz;
		float cloudCubeSample = CloudShadowsTexture.SampleLevel(textureSampler, cloudSampleDir, 0).x;
		return lerp(1.0, 1.0 - cloudCubeSample, SharedData::cloudShadowsSettings.Opacity);
	}
}
