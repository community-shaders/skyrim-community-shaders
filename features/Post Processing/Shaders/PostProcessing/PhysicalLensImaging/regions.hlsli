#ifndef PHYSICAL_LENS_REGIONS
#define PHYSICAL_LENS_REGIONS
// ExtractTile partitions integer pixels at floor(group * size / grid).
// Invert those exact bounds, including odd sizes. floor(uv * grid) assigns
// boundary pixels to a different owner and leaves their tail energy unremoved.
uint2 WaveRegionTile(float2 uv, uint2 size)
{
	uint2 pixel = min(uint2(saturate(uv) * size), size - 1);
	return min(((pixel + 1) * uint2(64, 36) - 1) / size, uint2(63, 35));
}
#endif
