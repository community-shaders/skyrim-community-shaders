// Always kPOST_ZPREPASS_COPY, never TerrainBlending's blendedDepthTexture: that one is produced at
// end of frame and would be a frame stale here, which made grass flicker on fast camera movement.
Texture2D<unorm float> SrcDepth : register(t0);

RWTexture2D<float> HiZ : register(u0);

// Must match HiZPyramid::kDownsampleFactor.
#define HIZ_DOWNSAMPLE_FACTOR 4

cbuffer HiZParams : register(b0)
{
	uint2 SrcSize;
	uint2 DstSize;
};

[numthreads(8, 8, 1)] void main(uint3 tid : SV_DispatchThreadID) {
	if (any(tid.xy >= DstSize))
		return;

	const int2 src = int2(tid.xy) * HIZ_DOWNSAMPLE_FACTOR;

	// Out of bounds reads as far, so an edge tile reduces to 1.0 and can only under-cull.
	float d = 0.0;
	[unroll] for (int y = 0; y < HIZ_DOWNSAMPLE_FACTOR; ++y)
	{
		[unroll] for (int x = 0; x < HIZ_DOWNSAMPLE_FACTOR; ++x)
		{
			const int2 p = src + int2(x, y);
			d = max(d, all(p < int2(SrcSize)) ? SrcDepth.Load(int3(p, 0)) : 1.0);
		}
	}

	HiZ[tid.xy] = d;
}
