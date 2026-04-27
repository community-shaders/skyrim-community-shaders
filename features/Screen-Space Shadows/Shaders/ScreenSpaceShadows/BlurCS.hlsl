// 3x3 Gaussian blur.
//
// Kernel (sigma ~= 1, sum = 16):
//   1 2 1
//   2 4 2
//   1 2 1

Texture2D<float> shadowIn : register(t0);
RWTexture2D<unorm float> shadowOut : register(u0);

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
	uint pad2;
};

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	uint2 effectiveDim = max(uint2(1u, 1u), uint2(FrameDim) >> CurrentMip);
	if (any(float2(dtid) >= float2(effectiveDim)))
		return;

	int2 maxCoord = int2(max(uint2(1u, 1u), uint2(TexDim) >> CurrentMip)) - 1;

	float sum = 0.0;
	[unroll] for (int dy = -1; dy <= 1; dy++)
		[unroll] for (int dx = -1; dx <= 1; dx++)
			sum += shadowIn.Load(int3(clamp(int2(dtid) + int2(dx, dy), int2(0, 0), maxCoord), 0));

	shadowOut[dtid] = sum / 9.0;
}
