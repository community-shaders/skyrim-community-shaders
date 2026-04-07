#ifndef CLOUD_SHADOWS_HLSLI
#define CLOUD_SHADOWS_HLSLI

#ifndef CLOUD_SHADOW_REGISTER
#	define CLOUD_SHADOW_REGISTER t25
#endif

namespace CloudShadows
{
	TextureCube<float> CloudShadowsTexture : register(CLOUD_SHADOW_REGISTER);

	const static float CloudHeight = (2e3f / 1.428e-2) * 0.25;
	const static float PlanetRadius = (6371e3f / 1.428e-2);
	const static float RcpHPlusR = (1.0 / (CloudHeight + PlanetRadius));

	// Returns distance to the cloud layer along dir from rel_pos.
	// Returns -1 if rel_pos is above the cloud layer or dir points away.
	float IntersectCloudDist(float3 rel_pos, float3 dir)
	{
		float r = PlanetRadius;
		float3 p = (rel_pos + float3(0, 0, r)) * RcpHPlusR;
		float dotprod = dot(p, dir);
		float lengthsqr = dot(p, p);
		if (lengthsqr > 1)
			return -1;
		float t = -dotprod + sqrt(dotprod * dotprod - lengthsqr + 1);
		return t * (r + CloudHeight);
	}

	float3 GetCloudShadowSampleDir(float3 rel_pos, float3 eye_to_sun)
	{
		float cloudDist = IntersectCloudDist(rel_pos, eye_to_sun);
		if (cloudDist < 0)
			return eye_to_sun;
		return rel_pos + cloudDist * eye_to_sun;
	}

	float GetCloudShadowMult(float3 worldPosition, SamplerState textureSampler)
	{
		float3 cloudSampleDir = GetCloudShadowSampleDir(worldPosition, SharedData::DirLightDirection.xyz);
		float cloudCubeSample = CloudShadowsTexture.SampleLevel(textureSampler, cloudSampleDir, 0).x;
		return lerp(1.0, 1.0 - cloudCubeSample, SharedData::cloudShadowsSettings.Opacity);
	}
}
#endif
