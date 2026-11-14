#ifndef PHYSICAL_SKY_AP_SHADOW_HLSLI
#define PHYSICAL_SKY_AP_SHADOW_HLSLI

#ifndef AP_DIRECT_SHADOW_REGISTER
#	define AP_DIRECT_SHADOW_REGISTER t1
#endif
#ifndef TERRAIN_SHADOW_REGISTER
#	define TERRAIN_SHADOW_REGISTER t3
#endif
#include "CloudShadows/CloudShadows.hlsli"
#include "TerrainShadows/TerrainShadows.hlsli"

namespace AtmosphereShadow
{
	Texture2DArray<float4> TexDirectShadows : register(AP_DIRECT_SHADOW_REGISTER);

	struct DirectionalShadowLightData
	{
		column_major float4x4 ShadowProj[2];
		column_major float4x4 InvShadowProj[2];
		float2 EndSplitDistances;
		float2 StartSplitDistances;
	};
	StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

	const static uint nStep = 30;
	const static float rcpNStep = rcp(nStep);

	float3 GetVolumetricCloudTransmittance(float3 posWorldRel)
	{
		return GetDirlightTransmittance(posWorldRel + FrameBuffer::CameraPosAdjust.xyz, SampTr);
	}

	struct ShadowRayData
	{
		float2 endSplitDistances;
		float2 viewOriginZW;
		float2 viewDirectionZW;
		float3 lightOrigin[2];
		float3 lightDirection[2];
		bool terrainReady;
	};

	ShadowRayData BuildShadowRay(float3 dir)
	{
		uint lightCount, lightStride;
		DirectionalShadowLights.GetDimensions(lightCount, lightStride);
		uint shadowWidth, shadowHeight, shadowLayers;
		TexDirectShadows.GetDimensions(shadowWidth, shadowHeight, shadowLayers);
		DirectionalShadowLightData light = (DirectionalShadowLightData)0;
		if (lightCount > 0u && shadowWidth > 0u && shadowHeight > 0u && shadowLayers >= 2u)
			light = DirectionalShadowLights[0];
		ShadowRayData ray;
		uint terrainWidth, terrainHeight;
		TerrainShadows::ShadowHeightTexture.GetDimensions(terrainWidth, terrainHeight);
		ray.terrainReady = terrainWidth > 0u && terrainHeight > 0u;
		ray.endSplitDistances = light.EndSplitDistances;
		ray.viewOriginZW = mul(FrameBuffer::CameraViewProj, float4(0, 0, 0, 1)).zw;
		ray.viewDirectionZW = mul(FrameBuffer::CameraViewProj, float4(dir, 0)).zw;
		[unroll] for (uint cascade = 0; cascade < 2; ++cascade)
		{
			ray.lightOrigin[cascade] = mul(light.ShadowProj[cascade], float4(FrameBuffer::CameraPosAdjust.xyz, 1)).xyz;
			ray.lightDirection[cascade] = mul(light.ShadowProj[cascade], float4(dir, 0)).xyz;
		}
		return ray;
	}

	float SampleShadow(float3 posWorldRel, float distance, ShadowRayData ray)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;

		float shadow = 1.0;

		// Resolve scene and terrain occlusion first: a blocked sample needs neither
		// spherical cloud projection nor a 3D cloud-shadow lookup.
		{
			const float2 viewZW = ray.viewOriginZW + distance * ray.viewDirectionZW;
			float shadowMapDepth = SharedData::GetScreenDepth(viewZW.x / viewZW.y);

			[branch] if (ray.endSplitDistances.y > 0.0 &&
						 shadowMapDepth < ray.endSplitDistances.y)
			{
				uint cascadeIndex = shadowMapDepth >= ray.endSplitDistances.x ? 1u : 0u;

				float3 positionLS = ray.lightOrigin[cascadeIndex] + distance * ray.lightDirection[cascadeIndex];
				float4 depths = TexDirectShadows.GatherRed(SampTr, float3(saturate(positionLS.xy), cascadeIndex), 0);
				shadow *= dot(float4(depths > positionLS.z), 0.25);
			}
		}
		[branch] if (all(shadow < 1e-8)) return 0;

		// terrain shadow
		float3 posWorldAbs = posWorldRel + FrameBuffer::CameraPosAdjust.xyz;
		if (ray.terrainReady)
			shadow *= TerrainShadows::GetTerrainShadow(posWorldAbs, SampTr);
		[branch] if (all(shadow < 1e-8)) return 0;

		float cloudShadow = 1.0;
		[branch] if (SharedData::cloudShadowsSettings.Opacity != 0.0)
			cloudShadow = CloudShadows::GetCloudShadowMult(posWorldRel, SampTr);
		shadow *= Remap(cloudShadow, data.cloudShadowRemapRange.x, data.cloudShadowRemapRange.y, 0, 1);
		[branch] if (all(shadow < 1e-8)) return 0;

		shadow *= Remap(dot(GetVolumetricCloudTransmittance(posWorldRel), float3(0.2126, 0.7152, 0.0722)), data.cloudShadowRemapRange.x, data.cloudShadowRemapRange.y, 0, 1);
		[branch] if (all(shadow < 1e-8)) return 0;

		return shadow;
	}

	float TraceShadow(float3 dir, float dist, float jitter)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;

		if (dist <= 0.0)
			return 0.0;
		float distClamped = min(AP_MAX_DIST, dist);
		const ShadowRayData shadowRay = BuildShadowRay(dir);

		const float extGr = dot(data.rayleighScatter + data.aerosolAbsorption + data.aerosolScatter, 1 / 3.f);
		const float opticalDepth = extGr * distClamped;
		const float estContrib = 1 - exp(-opticalDepth);
		const float rcpExtGr = rcp(max(extGr, 1e-20));

		float shadow = 0;
		for (uint i = 1; i <= nStep; ++i) {
			const float sampleFraction = (i - 1 + jitter) * rcpNStep;
			float tSample;
			[branch] if (opticalDepth < 1e-3)
				tSample = sampleFraction * distClamped * (1.0 - 0.5 * (1.0 - sampleFraction) * opticalDepth);
			else tSample = -rcpExtGr * log(1 - sampleFraction * estContrib);

			float shadowSample = SampleShadow(dir * tSample, tSample, shadowRay);
			shadow += 1 - shadowSample;
		}

		return shadow * rcpNStep;
	}
}

#endif
