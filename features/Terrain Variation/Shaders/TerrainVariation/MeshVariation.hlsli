#ifndef TERRAIN_MESH_VARIATION_HLSLI
#define TERRAIN_MESH_VARIATION_HLSLI

inline StochasticOffsets ComputeStochasticOffsetsScaled(float2 uv, float scale)
{
	float2 skewUV = mul(SKEW_MATRIX, uv * scale);
	float2 vxID = floor(skewUV);
	float2 f = frac(skewUV);
	float bz = 1.0 - f.x - f.y;

	StochasticCorner c0, c1, c2;
	if (bz > 0) {
		c0.cell = vxID;
		c0.w = bz;
		c1.cell = vxID + float2(0, 1);
		c1.w = f.y;
		c2.cell = vxID + float2(1, 0);
		c2.w = f.x;
	} else {
		c0.cell = vxID + 1.0;
		c0.w = -bz;
		c1.cell = vxID + float2(1, 0);
		c1.w = 1.0 - f.y;
		c2.cell = vxID + float2(0, 1);
		c2.w = 1.0 - f.x;
	}

	if (c1.w > c0.w) {
		StochasticCorner t = c0;
		c0 = c1;
		c1 = t;
	}
	if (c2.w > c0.w) {
		StochasticCorner t = c0;
		c0 = c2;
		c2 = t;
	}
	if (c2.w > c1.w) {
		StochasticCorner t = c1;
		c1 = c2;
		c2 = t;
	}

	StochasticOffsets o;
	o.offset1 = hash2D2D(c0.cell);
	o.offset2 = hash2D2D(c1.cell);
	o.offset3 = 0;
	o.weights = float3(c0.w, c1.w, c2.w);
	return o;
}

inline StochasticOffsets ComputeStochasticOffsetsMesh(float2 meshUV)
{
	static const float meshScale = 1.0;
	return ComputeStochasticOffsetsScaled(meshUV, meshScale);
}

inline float4 StochasticEffectMesh(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets)
{
	return StochasticEffect(tex, samp, uv, offsets, 0.0);
}

inline float4 StochasticEffectParallaxMesh(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	return StochasticEffectParallax(tex, samp, uv, mipLevel, offsets);
}

#endif  // TERRAIN_MESH_VARIATION_HLSLI
