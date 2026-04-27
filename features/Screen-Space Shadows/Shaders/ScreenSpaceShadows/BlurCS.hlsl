// Depth-aware 3x3 bilateral blur.
// Weights each neighbour by exp(-dZ^2 / SurfaceThickness^2) so shadows don't
// bleed across depth discontinuities larger than SurfaceThickness.

#include "Common/SharedData.hlsli"

Texture2D<float> shadowIn : register(t0);
Texture2D<float> depthIn  : register(t1);
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

	float centerNDC = depthIn.Load(int3(dtid, 0));
	float centerZ = SharedData::GetScreenDepth(centerNDC);
	float sigma2 = SurfaceThickness * SurfaceThickness;

	float sum = 0.0;
	float weightSum = 0.0;

	[unroll] for (int dy = -1; dy <= 1; dy++)
	{
		[unroll] for (int dx = -1; dx <= 1; dx++)
		{
			int2 coord = clamp(int2(dtid) + int2(dx, dy), int2(0, 0), maxCoord);

			float sampleNDC = depthIn.Load(int3(coord, 0));
			float sampleZ = SharedData::GetScreenDepth(sampleNDC);

			float dz = centerZ - sampleZ;
			float w = exp(-dz * dz / sigma2);

			sum += shadowIn.Load(int3(coord, 0)) * w;
			weightSum += w;
		}
	}

	shadowOut[dtid] = sum / max(weightSum, 1e-5);
}
