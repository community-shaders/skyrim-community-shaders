// Depth-aware 2x bilateral upscale + min-combine.
// CurrentMip is the source mip level; output is at (CurrentMip-1) resolution.
// Caller binds depthSrc (CurrentMip depth) and depthDst ((CurrentMip-1) depth) as separate
// texDepthMip textures, so no mip-chain indexing is needed in this shader.

Texture2D<float> shadowLowRes : register(t0);  // shadow at CurrentMip resolution
Texture2D<float> depthSrc     : register(t1);  // depth at CurrentMip resolution
Texture2D<float> shadowDst    : register(t2);  // shadow at (CurrentMip-1) resolution
Texture2D<float> depthDst     : register(t3);  // depth at (CurrentMip-1) resolution

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
	uint pad;
};

// Soft weighted blend across all 15 non-empty permutations of the 4 bilinear neighbours,
// weighted by inverse squared depth error.  Avoids hard per-pixel snapping that causes
// pixelated edges where the winning permutation changes between adjacent output pixels.
float4 GetSelectiveBilateralWeights(float4 srcDepths, float dstDepth, float2 bilinearRatio)
{
	float4 bilinearWeights = float4(
		(1.0 - bilinearRatio.x) * (1.0 - bilinearRatio.y),
		bilinearRatio.x * (1.0 - bilinearRatio.y),
		(1.0 - bilinearRatio.x) * bilinearRatio.y,
		bilinearRatio.x * bilinearRatio.y);

	float4 resWeights = float4(0, 0, 0, 0);
	float totalWeight = 0.0;
	float eps = 1e-5;

	[unroll] for (int perm = 1; perm < 16; perm++)
	{
		float4 permMask = float4(
			float(perm & 1),
			float((perm >> 1) & 1),
			float((perm >> 2) & 1),
			float((perm >> 3) & 1));

		float4 weights = bilinearWeights * permMask;
		float weightSum = dot(weights, float4(1, 1, 1, 1));
		if (weightSum < eps)
			continue;
		weights /= weightSum;

		float interpDepth = dot(srcDepths, weights);
		float d = interpDepth - dstDepth;
		float permWeight = 1.0 / max(eps, d * d);

		resWeights += weights * permWeight;
		totalWeight += permWeight;
	}
	return resWeights / max(eps, totalWeight);
}

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	uint outputMip = CurrentMip - 1;
	uint2 outputDim = uint2(FrameDim) >> outputMip;
	if (any(float2(dtid) >= float2(outputDim)))
		return;

	// Reference depth for this output pixel, at (CurrentMip-1) resolution.
	float refDepth = depthDst.Load(int3(dtid, 0));

	// Always 2x upscale — map output texel centre into source texel space.
	int2 srcMaxCoord = max(int2(1, 1), int2(TexDim) >> CurrentMip) - 1;
	float2 srcCoordF = (float2(dtid) + 0.5) / 2.0 - 0.5;
	int2 baseIdx = int2(floor(srcCoordF));
	float2 ratio = frac(srcCoordF);

	static const int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

	float4 srcDepths, srcShadows;
	[unroll] for (int i = 0; i < 4; i++)
	{
		int2 coord = clamp(baseIdx + offsets[i], int2(0, 0), srcMaxCoord);
		srcDepths[i] = depthSrc.Load(int3(coord, 0));
		srcShadows[i] = shadowLowRes.Load(int3(coord, 0));
	}

	float4 weights = GetSelectiveBilateralWeights(srcDepths, refDepth, ratio);
	float upscaled = dot(srcShadows, weights);

	// Min-combine with the shadow already computed at this output resolution.
	float sameLevel = shadowDst.Load(int3(dtid, 0));
	upscaledOut[dtid] = min(upscaled, sameLevel);
}
