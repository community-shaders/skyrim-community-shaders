#ifndef PHYSICAL_SKY_CLOUD_BOUNDS_HLSLI
#define PHYSICAL_SKY_CLOUD_BOUNDS_HLSLI

RWTexture2D<float2> RWCloudHeightBounds : register(u6);
RWTexture2D<float4> RWCloudHeightTiles : register(u6);
RWTexture2D<float2> RWCloudWeatherBounds : register(u5);
groupshared float4 CloudHeightBoundsShared[256];

void ReduceCloudHeightBounds(uint index, uint count)
{
	GroupMemoryBarrierWithGroupSync();
	[loop] for (uint stride = count / 2u; stride > 0u; stride >>= 1u)
	{
		if (index < stride) {
			const float4 other = CloudHeightBoundsShared[index + stride];
			CloudHeightBoundsShared[index] = float4(min(CloudHeightBoundsShared[index].x, other.x),
				max(CloudHeightBoundsShared[index].yzw, other.yzw));
		}
		GroupMemoryBarrierWithGroupSync();
	}
}

[numthreads(8, 8, 1)] void buildCloudHeightBounds(uint2 group : SV_GroupID, uint2 thread : SV_GroupThreadID, uint index : SV_GroupIndex) {
	uint2 size, tiles;
	TexCloudHeight.GetDimensions(size.x, size.y);
	RWCloudHeightTiles.GetDimensions(tiles.x, tiles.y);
	const uint2 tileSize = (size + tiles - 1u) / tiles;
	const uint2 origin = group * tileSize;
	const uint2 end = min(origin + tileSize, size);
	// Preserve the original height range in XY. ZW also bound the lower
	// surface and all modeling types, including sheared/upstream samples.
	float4 bounds = float4(1, 0, 0, 0);
	[loop] for (uint y = origin.y + thread.y; y < end.y; y += 8u)
	{
		[loop] for (uint x = origin.x + thread.x; x < end.x; x += 8u)
		{
			const float2 heights = saturate(TexCloudHeight.Load(int3(x, y, 0)).rg);
			const float topType = saturate(TexCloudModeling.Load(int3(x, y, 0)).g);
			bounds = float4(min(bounds.x, heights.x), max(bounds.y, heights.y), max(bounds.z, heights.x), max(bounds.w, topType));
		}
	}
	CloudHeightBoundsShared[index] = bounds;
	ReduceCloudHeightBounds(index, 64u);
	if (index == 0u)
		RWCloudHeightTiles[group] = CloudHeightBoundsShared[0];
}

	[numthreads(16, 16, 1)] void reduceCloudHeightBounds(uint2 pixel : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
	CloudHeightBoundsShared[index] = TexCloudHeightBoundsTiles[pixel];
	ReduceCloudHeightBounds(index, 256u);
	const float4 bounds = CloudHeightBoundsShared[0];
	if (index == 0u)
		RWCloudHeightBounds[uint2(0, 0)] = bounds.xy;
	// Find support in the actual LUT, without assuming that cloud type is
	// monotonic. Bilinear filtering reaches half a texel past the next centre.
	uint2 size;
	TexCloudProfileLUT.GetDimensions(size.x, size.y);
	const uint lastType = min((uint)max(ceil(bounds.w * size.x - 0.5), 0.0), size.x - 1u);
	float support = 0.0;
	[loop] for (uint texel = index; texel < size.x * size.y; texel += 256u)
	{
		const uint2 address = uint2(texel % size.x, texel / size.x);
		if (address.x <= lastType && TexCloudProfileLUT.Load(int3(address, 0)).g > 0.0)
			support = max(support, saturate((address.y + 1.5) / size.y));
	}
	if (VolumetricCloudBuffer[0].topExpansion > 0.0) {
		TexCloudAdjustmentLUT.GetDimensions(size.x, size.y);
		const uint lastExpandedType = min((uint)max(ceil(bounds.w * size.x - 0.5), 0.0), size.x - 1u);
		[loop] for (uint texel = index; texel < size.x * size.y; texel += 256u)
		{
			const uint2 address = uint2(texel % size.x, texel / size.x);
			if (address.x <= lastExpandedType && TexCloudAdjustmentLUT.Load(int3(address, 0)).r > 0.0)
				support = max(support, saturate((address.y + 1.5) / size.y));
		}
	}
	// All threads must finish reading the first reduction before reusing it.
	GroupMemoryBarrierWithGroupSync();
	CloudHeightBoundsShared[index] = float4(0.0, support, 0.0, 0.0);
	ReduceCloudHeightBounds(index, 256u);
	if (index == 0u)
		RWCloudWeatherBounds[uint2(0, 0)] = float2(bounds.x, lerp(bounds.z, bounds.y, CloudHeightBoundsShared[0].y));
}

#endif
