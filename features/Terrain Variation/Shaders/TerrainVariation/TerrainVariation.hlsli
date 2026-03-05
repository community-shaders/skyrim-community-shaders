// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS --------------------- //
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
// Distance-based sample reduction
static const float MIP_1SAMPLE = 3.0;   // Mip level for single-sample path
static const float MIP_BUMP = 1.41421356;  // exp2(0.5) gradient scale for +0.5 mip bump
// Importance sampling thresholds
static const float HEIGHT_INFLUENCE = 0.3;
static const float LOW_WEIGHT_THRESHOLD = 0.15;  // Layer blend weight below which 1-sample is used

// --------------------- STRUCTURES --------------------- //
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

struct StochasticGradients
{
	float2 uvDx;
	float2 uvDy;
};

// --------------------- HASH FUNCTIONS --------------------- //
inline float2 hash2D2D(float2 s)
{
	s = frac(s * HASH_MULTIPLIER);
	s += dot(s, s.yx + 19.19);
	return frac((s.xx + s.yy) * s.yx);
}

inline float2 hashLOD(float2 p)
{
	p = frac(p * 0.318);
	return frac(p.x + p.y * float2(17.0, 23.0));
}

// --------------------- COMPUTE FUNCTIONS --------------------- //
inline StochasticGradients ComputeStochasticGradients(float2 uv)
{
	StochasticGradients g;
	float biasScale = exp2(SharedData::MipBias);
	g.uvDx = ddx(uv) * biasScale;
	g.uvDy = ddy(uv) * biasScale;
	return g;
}

inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
	float2 skewUV = mul(SKEW_MATRIX, landscapeUV * WORLD_SCALE);
	float2 vxID = floor(skewUV);
	float2 f = frac(skewUV);
	float bz = 1.0 - f.x - f.y;

	float2 v0, v1, v2;
	float3 barry;
	if (bz > 0) {
		v0 = vxID;
		v1 = vxID + float2(0, 1);
		v2 = vxID + float2(1, 0);
		barry = float3(bz, f.y, f.x);
	} else {
		v0 = vxID + 1.0;
		v1 = vxID + float2(1, 0);
		v2 = vxID + float2(0, 1);
		barry = float3(-bz, 1.0 - f.y, 1.0 - f.x);
	}

	StochasticOffsets o;
	o.offset1 = hash2D2D(v0);
	o.offset2 = hash2D2D(v1);
	o.offset3 = hash2D2D(v2);
	o.weights = barry;

	// Sort descending by barycentric weight (importance sampling)
	if (o.weights.y > o.weights.x) {
		float2 tO = o.offset1; o.offset1 = o.offset2; o.offset2 = tO;
		float tW = o.weights.x; o.weights.x = o.weights.y; o.weights.y = tW;
	}
	if (o.weights.z > o.weights.x) {
		float2 tO = o.offset1; o.offset1 = o.offset3; o.offset3 = tO;
		float tW = o.weights.x; o.weights.x = o.weights.z; o.weights.z = tW;
	}
	if (o.weights.z > o.weights.y) {
		float2 tO = o.offset2; o.offset2 = o.offset3; o.offset3 = tO;
		float tW = o.weights.y; o.weights.y = o.weights.z; o.weights.z = tW;
	}

	return o;
}

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 landscapeUV)
{
	float2 cellID = floor(landscapeUV * 255437.0);
	float2 h1 = hashLOD(cellID);
	float2 h2 = hashLOD(cellID + 127.0);

	StochasticOffsets o;
	o.offset1 = h1 * 0.08;
	o.offset2 = h2 * 0.08;
	o.offset3 = 0;
	o.weights = float3(0.65, 0.35, 0.0);
	return o;
}

// --------------------- SAMPLING FUNCTIONS --------------------- //

// LOD terrain stochastic sampling — 2 SampleBias, fixed blend
inline float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD)
{
	float2 dir1 = float2(rnd - 0.5, frac(rnd * 1.618) - 0.5);
	float4 s1 = tex.SampleBias(samp, uv + (offsetsLOD.offset1 + dir1) * 0.01, SharedData::MipBias);
	float4 s2 = tex.SampleBias(samp, uv + (offsetsLOD.offset2 + float2(dir1.y, -dir1.x)) * 0.01, SharedData::MipBias);
	return lerp(s2, s1, 0.65);
}

// Stochastic sampling with importance-based sample reduction.
// Layer blend weight controls quality: low-weight layers get cheap 1-sample path.
// 2-sample height blend (Jason Booth technique) for close-range high-weight layers.
// Branchless blend is smooth across simplex boundaries and compile-time friendly.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float mipLevel, StochasticGradients grad, float layerWeight)
{
	// Far distance or low-importance layer: single offset with bumped mip for coverage
	if (mipLevel >= MIP_1SAMPLE || layerWeight < LOW_WEIGHT_THRESHOLD)
		return tex.SampleGrad(samp, uv + offsets.offset1, grad.uvDx * MIP_BUMP, grad.uvDy * MIP_BUMP);

	// 2-sample height-blended: barycentric weights squared x height-driven importance
	float4 s1 = tex.SampleGrad(samp, uv + offsets.offset1, grad.uvDx, grad.uvDy);
	float4 s2 = tex.SampleGrad(samp, uv + offsets.offset2, grad.uvDx, grad.uvDy);
	float w1 = offsets.weights.x * offsets.weights.x * (1.0 + HEIGHT_INFLUENCE * dot(s1.rgb, float3(0.2126, 0.7152, 0.0722)));
	float w2 = offsets.weights.y * offsets.weights.y * (1.0 + HEIGHT_INFLUENCE * dot(s2.rgb, float3(0.2126, 0.7152, 0.0722)));
	return lerp(s2, s1, w1 * rcp(w1 + w2));
}

inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	// weights are sorted descending so weights.z is always the smallest (0 at edges/vertices,
	// max 0.333 at the centroid). Below the threshold it contributes negligible height error,
	// so skip the third fetch. The branch is coherent at simplex-triangle scale (>> quad size).
	if (offsets.weights.z < 0.1)
		return (s1 * offsets.weights.x + s2 * offsets.weights.y) * rcp(offsets.weights.x + offsets.weights.y);
	float4 s3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);
	return s1 * offsets.weights.x + s2 * offsets.weights.y + s3 * offsets.weights.z;
}

// Unified terrain sampling with importance-aware stochastic when enabled.
// layerWeight is the Skyrim landscape blend weight — the natural importance signal
// for the 6-layer terrain system. Low-weight layers get cheaper 1-sample treatment.
inline float4 SampleTerrain(bool enabled, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float mipLevel, StochasticGradients grad, float layerWeight)
{
	[branch] if (enabled)
		return StochasticEffect(tex, samp, uv, offsets, mipLevel, grad, layerWeight);
	return tex.SampleBias(samp, uv, SharedData::MipBias);
}

#endif  // TERRAIN_VARIATION_HLSLI