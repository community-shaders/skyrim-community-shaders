#ifndef CLOUD_SHADOWS_HLSLI
#define CLOUD_SHADOWS_HLSLI

#include "Common/Game.hlsli"

namespace CloudShadows
{
	TextureCube<float> CloudShadowsTexture : register(t25);
#if defined(CLOUDS)
	// Cloud occlusion of the layers drawn up to the current one, for cloud self shadowing
	TextureCube<float> CloudSelfShadowTexture : register(t26);
#endif

	const static float CloudHeight = (2e3f / GAME_UNIT_TO_M);
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
		return saturate(1.0 - cloudCubeSample * SharedData::cloudShadowsSettings.Opacity);
	}

#if defined(CLOUD_SHADOWS_CAPTURE)
	cbuffer CloudShadowsCapture : register(b7)
	{
		uint CaptureFace;
	};

	/** @brief Clip position of an eye-relative direction on cube face CaptureFace, the inverse of DynamicCubemaps' GetCubemapSamplingVector. */
	float4 GetCaptureClipPosition(float3 direction)
	{
		float3 clip;
		switch (CaptureFace) {
		case 0:
			clip = float3(-direction.z, direction.y, direction.x);
			break;
		case 1:
			clip = float3(direction.z, direction.y, -direction.x);
			break;
		case 2:
			clip = float3(direction.x, -direction.z, direction.y);
			break;
		case 3:
			clip = float3(direction.x, direction.z, -direction.y);
			break;
		case 4:
			clip = float3(direction.x, direction.y, direction.z);
			break;
		default:
			clip = float3(-direction.x, direction.y, -direction.z);
			break;
		}
		return clip.xyzz;
	}
#endif
}

#endif
