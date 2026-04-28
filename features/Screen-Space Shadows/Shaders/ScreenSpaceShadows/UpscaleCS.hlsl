// 2x upscale via 3x3 box blur over source texels + min-combine.
// CurrentMip is the source mip level; output is at (CurrentMip-1) resolution.
// depthSrc and depthDst are unused but kept in the binding layout so the caller
// does not need to special-case this shader.

Texture2D<float> shadowLowRes : register(t0);  // shadow at CurrentMip resolution
Texture2D<float2> depthSrc    : register(t1);  // unused (kept for binding compatibility)
Texture2D<float>  shadowDst   : register(t2);  // shadow at (CurrentMip-1) resolution
Texture2D<float2> depthDst    : register(t3);  // unused (kept for binding compatibility)

RWTexture2D<unorm float> upscaledOut : register(u0);

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
	uint outputMip = CurrentMip - 1;
	uint2 outputDim = uint2(FrameDim) >> outputMip;
	if (any(float2(dtid) >= float2(outputDim)))
		return;

	// Map output texel centre into source (half-res) texel space.
	int2 srcMaxCoord = max(int2(1, 1), int2(TexDim) >> CurrentMip) - 1;
	// Centre of this output pixel maps to srcCoordF in source-texel space.
	int2 srcBase = int2(dtid) / 2;

	// 3x3 box blur over source texels centred on the nearest source texel.
	float sum = 0.0;
	[unroll] for (int dy = -1; dy <= 1; dy++)
	{
		[unroll] for (int dx = -1; dx <= 1; dx++)
		{
			int2 coord = clamp(srcBase + int2(dx, dy), int2(0, 0), srcMaxCoord);
			sum += shadowLowRes.Load(int3(coord, 0));
		}
	}
	float upscaled = sum / 9.0;

	// Min-combine with the shadow already computed at this output resolution.
	float sameLevel = shadowDst.Load(int3(dtid, 0));
	upscaledOut[dtid] = min(upscaled, sameLevel);
}
