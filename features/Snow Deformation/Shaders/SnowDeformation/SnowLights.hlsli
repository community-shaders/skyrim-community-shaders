#ifndef __SNOW_LIGHTS_DEPENDENCY_HLSL__
#define __SNOW_LIGHTS_DEPENDENCY_HLSL__

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

#include "LightLimitFix/LightLimitFix.hlsli"

namespace SnowLights
{
	uint ClusterLightCount(float2 clusterUV, float viewZ)
	{
		uint clusterIndex = 0;
		uint count = 0;
		[branch] if (LightLimitFix::GetClusterIndex(clusterUV, viewZ, clusterIndex))
			count = LightLimitFix::lightGrid[clusterIndex].lightCount;
		return count;
	}

	void AccumulatePointLights(
		SnowMaterialCtx mtl,
		float3 worldPos, float3 worldPosAbs, float3 normalWS, float3 V, float viewZ,
		float2 clusterUV, float2 glintUV, float2 uvDDX, float2 uvDDY,
		float4 gameShadowMask, float4 markSpecular,
		inout float3 diffuse, inout float3 specular, inout float3 markLit)
	{
		uint clusterIndex = 0;
		[branch] if (!LightLimitFix::GetClusterIndex(clusterUV, viewZ, clusterIndex))
			return;

		LightLimitFix::LightGrid grid = LightLimitFix::lightGrid[clusterIndex];

		[loop] for (uint i = 0; i < grid.lightCount; i++)
		{
			LightLimitFix::Light light = LightLimitFix::lights[LightLimitFix::lightList[grid.offset + i]];

			float3 lightDirection = light.positionWS.xyz - worldPos;
			float lightDist = length(lightDirection);
			float attenuation = LightLimitFix::GetAttenuation(lightDist, light);
			if (attenuation < 1e-5)
				continue;

			float3 lightColor = Color::PointLight(light.color.xyz) * attenuation * light.fade;

			float lightShadow = 1.0;
			[branch] if (light.lightFlags & LightLimitFix::LightFlags::Shadow)
			{
				[branch] if (gameShadowMask.x >= 0.0)
					lightShadow = gameShadowMask[min(light.shadowLightIndex, 3u)];
				else
					lightShadow = SnowShadow::GetPointLightShadow(worldPosAbs, light.shadowLightIndex, light.radius);
			}

			float3 L = normalize(lightDirection);
			if (dot(normalWS, L) <= 0.0 || lightShadow <= 0.0)
				continue;

			SnowEvaluateLightPBR(mtl, normalWS, V, L, lightColor, lightShadow,
				glintUV, uvDDX, uvDDY, markSpecular, diffuse, specular, markLit);
		}
	}
}

#endif
