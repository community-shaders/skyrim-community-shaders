// Build Hi-Z depth pyramid: MIN downsample per mip level
// Reversed-Z: min = farthest surface = conservative for cell-boundary ray marching
// (if ray is in front of farthest surface, safe to skip entire cell)

Texture2D<float> srcMip : register(t0);
RWTexture2D<float> dstMip : register(u0);

[numthreads(8, 8, 1)] void main(uint2 dtid
								 : SV_DispatchThreadID)
{
	uint2 srcCoord = dtid * 2;

	float d00 = srcMip[srcCoord + uint2(0, 0)];
	float d10 = srcMip[srcCoord + uint2(1, 0)];
	float d01 = srcMip[srcCoord + uint2(0, 1)];
	float d11 = srcMip[srcCoord + uint2(1, 1)];

	// MIN for reversed-Z (farthest surface is the conservative bound for skip tests)
	dstMip[dtid] = min(min(d00, d10), min(d01, d11));
}
