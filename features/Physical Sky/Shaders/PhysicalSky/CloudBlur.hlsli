#ifndef PHYSICAL_SKY_CLOUD_BLUR_HLSLI
#define PHYSICAL_SKY_CLOUD_BLUR_HLSLI

Texture2D<float> TexCloudResultTr : register(t29);
Texture2D<float4> TexCloudResultLum : register(t30);
Texture2D<float4> TexCloudResultAux : register(t31);
RWTexture2D<float> RWTexFilteredTr : register(u3);
RWTexture2D<float4> RWTexFilteredLum : register(u4);
RWTexture2D<float4> RWTexFilteredAux : register(u5);

#ifndef CLOUD_BLUR_RADIUS
#	define CLOUD_BLUR_RADIUS 1
#endif
static const uint CloudFilterWidth = 8u + 2u * CLOUD_BLUR_RADIUS;
static const uint CloudFilterCount = CloudFilterWidth * CloudFilterWidth;
groupshared float CloudFilterTr[CloudFilterCount];
groupshared float4 CloudFilterLum[CloudFilterCount];
groupshared float4 CloudFilterAux[CloudFilterCount];

[numthreads(8, 8, 1)] void filterCloud(uint2 pixel : SV_DispatchThreadID, uint2 thread : SV_GroupThreadID, uint lane : SV_GroupIndex) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const int2 maximum = int2(info.activeFrameDim) - 1;
	const int2 origin = int2(pixel - thread) - CLOUD_BLUR_RADIUS;
	[unroll] for (uint load = 0u; load < (CloudFilterCount + 63u) / 64u; ++load)
	{
		const uint index = lane + load * 64u;
		if (index >= CloudFilterCount)
			continue;
		const int2 samplePixel = clamp(origin + int2(index % CloudFilterWidth, index / CloudFilterWidth), 0, maximum);
		CloudFilterTr[index] = TexCloudResultTr[samplePixel];
		CloudFilterLum[index] = TexCloudResultLum[samplePixel];
		CloudFilterAux[index] = TexCloudResultAux[samplePixel];
	}
	GroupMemoryBarrierWithGroupSync();
	if (any(pixel >= uint2(info.activeFrameDim)))
		return;
	const uint center = (thread.y + CLOUD_BLUR_RADIUS) * CloudFilterWidth + thread.x + CLOUD_BLUR_RADIUS;
	const float4 metadata = CloudFilterAux[center];
	const float falloff = 1.0 - saturate(abs(metadata.x - 1.0) / 6.0);
	const float radius = (pow(falloff, 4.0) * 0.67 + 0.33) * CLOUD_BLUR_RADIUS;
	float4 color = 0.0;
	float transmittance = 0.0;
	float2 transmittanceRange = float2(1, 0);
	float weightSum = 0.0;
	[unroll] for (int y = -CLOUD_BLUR_RADIUS; y <= CLOUD_BLUR_RADIUS; ++y)
	{
		[unroll] for (int x = -CLOUD_BLUR_RADIUS; x <= CLOUD_BLUR_RADIUS; ++x)
		{
			const uint sampleIndex = uint(int(center) + y * int(CloudFilterWidth) + x);
			const float sceneDepth = CloudFilterAux[sampleIndex].y;
			if (abs(sceneDepth - metadata.y) > 0.064)
				continue;
#if CLOUD_BLUR_RADIUS == 1
			const float weight = (x == 0 ? 1.0 - radius : radius * 0.5) * (y == 0 ? 1.0 - radius : radius * 0.5);
#else
			// Expand the four bilinear samples at (+/-radius, +/-radius) into
			// texel weights, preserving the scene-depth rejection at each texel.
			const float2 axisWeight = (saturate(1.0 - abs(float2(x, y) - radius)) + saturate(1.0 - abs(float2(x, y) + radius))) * 0.5;
			const float weight = axisWeight.x * axisWeight.y;
#endif
			const float sampleTr = CloudFilterTr[sampleIndex];
			transmittanceRange = float2(min(transmittanceRange.x, sampleTr), max(transmittanceRange.y, sampleTr));
			transmittance += sampleTr * weight;
			color += CloudFilterLum[sampleIndex] * weight;
			weightSum += weight;
		}
	}
	color = weightSum > 0.0 ? color / weightSum : CloudFilterLum[center];
	RWTexFilteredTr[pixel] = weightSum > 0.0 ? clamp(transmittance / weightSum, transmittanceRange.x, transmittanceRange.y) : CloudFilterTr[center];
	RWTexFilteredLum[pixel] = color;
	RWTexFilteredAux[pixel] = metadata;
}

#endif
