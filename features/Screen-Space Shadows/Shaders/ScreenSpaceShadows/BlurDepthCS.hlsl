// 4x4 box blur on a single depth-moment mip level.
// Both channels (linearZ, linearZ²) are blurred together so the averaged
// moments remain valid for downstream VSM queries.
// CurrentMip selects which mip resolution we operate at; the source and
// destination textures are bound as separate resources to avoid SRV/UAV hazards.

Texture2D<float2> srcDepth : register(t0);
RWTexture2D<float2> dstDepth : register(u0);

cbuffer SSSCB : register(b1)
{
	float2 FrameDim;
	float2 RcpTexDim;

	float2 TexDim;
	float2 DynamicRes;

	float SurfaceThickness;
	float ShadowContrast;
	float RayLength;
	uint CurrentMip;

	float3 LightWorldDir;
};

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	uint2 effectiveDim = max(uint2(1u, 1u), uint2(FrameDim) >> CurrentMip);
	if (any(float2(dtid) >= float2(effectiveDim)))
		return;

	int2 maxCoord = int2(max(uint2(1u, 1u), uint2(TexDim) >> CurrentMip)) - 1;

	float2 sum = 0;
	float count = 0;

	[unroll] for (int dy = -1; dy <= 2; dy++)
	{
		[unroll] for (int dx = -1; dx <= 2; dx++)
		{
			int2 c = clamp(int2(dtid) + int2(dx, dy), int2(0, 0), maxCoord);
			float2 s = srcDepth.Load(int3(c, 0));
			// Skip sentinel (0,0) so sky pixels don't drag down neighbour moments.
			if (s.x > 0.0) {
				sum += s;
				count += 1.0;
			}
		}
	}

	dstDepth[dtid] = count > 0.0 ? sum / count : float2(0, 0);
}
