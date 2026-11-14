#ifndef COMMON_VIEW_MEDIUM_HLSLI
#define COMMON_VIEW_MEDIUM_HLSLI

#include "Common/ViewAtmosphere.hlsli"

#ifdef PHYSICAL_SKY
#	include "PhysicalSky/Common.hlsli"
#endif

#ifdef MEDIUM_COMPOSITE
Texture2D<float4> TexMediumCloudDepth : register(t41);
#else
Texture2D<float4> TexMediumCloudDepth : register(t116);
#endif

namespace ViewMedium
{
	ViewAtmosphere::Medium SampleHeightMedium(float3 position, float2 screenUV, SamplerState samplerState)
	{
		if (ViewAtmosphere::Enabled()) {
			const float4 clip = mul(FrameBuffer::CameraViewProjUnjittered, float4(position, 1));
			const float2 volumeUv = clip.xy / max(clip.w, 1e-6) * float2(0.5, -0.5) + 0.5;
			return ViewAtmosphere::Sample(volumeUv, length(position), samplerState);
		}
		ViewAtmosphere::Medium result = ViewAtmosphere::Identity();
#ifdef EXP_HEIGHT_FOG
		if (SharedData::exponentialHeightFogSettings.enabled && dot(position, position) > 1e-8) {
			const float4 fog = ExponentialHeightFog::GetExponentialHeightFog(position, FrameBuffer::CameraPosAdjust.xyz, 0.0.xxx);
			result.luminance = ColorManagement::SceneToLinear(fog.rgb) * fog.a;
			result.transmittance = 1.0 - fog.a;
		}
#endif
		return result;
	}

	ViewAtmosphere::Medium SampleViewMedium(float3 position, float2 screenUV, SamplerState samplerState, bool fullCloudRay)
	{
		if (fullCloudRay && ViewAtmosphere::Enabled())
			position = normalize(position) * ViewAtmosphere::Data[0].grids[2].depth.y;
		ViewAtmosphere::Medium medium = SampleHeightMedium(position, screenUV, samplerState);

#ifdef PHYSICAL_SKY
		if (!SharedData::physSkyData.enabled || !SharedData::physSkyData.enableVolumetricClouds)
			return medium;
		const float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenUV);
		uint2 dimensions;
		TexMediumCloudDepth.GetDimensions(dimensions.x, dimensions.y);
		const float2 maximum = max(floor(float2(dimensions) * FrameBuffer::DynamicResolutionParams1.xy) - 0.5, 0.5);
		const float2 sampleUv = clamp(uv * float2(dimensions), 0.5, maximum) / float2(dimensions);
		const float4 metadata = TexMediumCloudDepth.SampleLevel(samplerState, sampleUv, 0);
		const float cloudDistance = metadata.x * 1000.0 / GAME_UNIT_TO_M;
		const float distance = length(position);
		const float visibility = fullCloudRay ? 1.0 : saturate((distance - cloudDistance) * GAME_UNIT_TO_M * 0.025);
		const float4 cloud = PhysSky::TexVolLum.SampleLevel(samplerState, sampleUv, 0);
		precise float cloudT = (1.0 - visibility) + visibility * PhysSky::TexVolTr.SampleLevel(samplerState, sampleUv, 0);
		if (cloudT >= 1.0)
			return medium;
		const float3 cloudL = cloud.rgb * visibility;
		const ViewAtmosphere::Medium front = SampleHeightMedium(position * (min(cloudDistance, distance) / max(distance, 1e-6)), screenUV, samplerState);
		medium.luminance = lerp(front.luminance, medium.luminance, cloudT) + front.transmittance * cloudL;
		medium.transmittance *= cloudT;
#endif
		return medium;
	}

	ViewAtmosphere::Medium SampleViewMedium(float3 position, float2 screenUV, SamplerState samplerState)
	{
		return SampleViewMedium(position, screenUV, samplerState, false);
	}

	float3 CompositeViewMedium(float3 color, float3 position, float2 screenUV, SamplerState samplerState, bool fullCloudRay)
	{
		const ViewAtmosphere::Medium medium = SampleViewMedium(position, screenUV, samplerState, fullCloudRay);
		float3 luminance = medium.luminance;
#if defined(PHYSICAL_SKY) && defined(MEDIUM_COMPOSITE)
		if (SharedData::physSkyData.lightningColorIntensity.w > 0.0) {
			const float4 channel = PhysSky::TexLightningChannels.SampleLevel(samplerState, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenUV), 0);
			const float distance = channel.a * 1000.0 / GAME_UNIT_TO_M;
			if (distance > 0.0 && (fullCloudRay || distance < length(position)))
				luminance += channel.rgb * SampleHeightMedium(normalize(position) * distance, screenUV, samplerState).transmittance;
		}
#endif
		return ColorManagement::SceneColor::ScaleAndAddLinear(color, medium.transmittance, luminance);
	}
}

#endif
