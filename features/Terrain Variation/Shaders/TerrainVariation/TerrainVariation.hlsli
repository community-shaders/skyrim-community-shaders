// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS --------------------- //
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
// Height-vs-stochastic weight (unchanged). Stochastic vertex weights use HEIGHT_INFLUENCE only via heightInfluence below.
static const float HEIGHT_INFLUENCE = 0.3;
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);
static const float HEIGHT_BLEND_FADE_MIP_START = 1.6;
static const float HEIGHT_BLEND_FADE_MIP_RANGE = 2.2;
// TV distant/minified fallback: single sample with primary offset (skip dual-sample blend only).
// The second sample's weight fades to zero across [START - FADE_RANGE, START] so the single-sample
// branch is reached only once it contributes nothing -> no pop/seam at the transition.
static const float TV_SINGLE_SAMPLE_MIP_START = 3.0;
static const float TV_SINGLE_SAMPLE_FADE_RANGE = 0.6;
static const float TV_SINGLE_SAMPLE_FADE_RCP = 1.0 / TV_SINGLE_SAMPLE_FADE_RANGE;
// Golden ratio for frac(rnd * φ) low-discrepancy jitter; precompute once per pixel at callsite when possible.
static const float STOCHASTIC_LOD_PHI = 1.618;

// --------------------- STRUCTURES --------------------- //
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

// Triangle corner for barycentric sort: pack cell id + weight so each swap updates both.
struct StochasticCorner
{
	float2 cell;
	float w;
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
	return frac(float2(dot(p, float2(1.0, 17.0)), dot(p, float2(1.0, 23.0))));
}

// --------------------- COMPUTE FUNCTIONS --------------------- //
inline StochasticOffsets ZeroStochasticOffsets()
{
	StochasticOffsets o;
	o.offset1 = 0;
	o.offset2 = 0;
	o.offset3 = 0;
	o.weights = 0;
	return o;
}

inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
	float2 skewUV = mul(SKEW_MATRIX, landscapeUV * WORLD_SCALE);
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

	// Sort by weight descending (3-comparator network). Only c0/c1 are hashed; weights.xy must be the two largest.
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

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 landscapeUV)
{
	StochasticOffsets o = ZeroStochasticOffsets();
	if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix) {
		float2 cellID = floor(landscapeUV * 255437.0);
		float2 h1 = hashLOD(cellID);
		float2 h2 = hashLOD(cellID + 127.0);
		o.offset1 = h1 * 0.08;
		o.offset2 = h2 * 0.08;
		o.weights = float3(0.65, 0.35, 0.0);
	}
	return o;
}

// --------------------- SAMPLING FUNCTIONS --------------------- //

inline float2 StochasticSampleLODJitter(float rnd)
{
	return float2(rnd - 0.5, frac(rnd * STOCHASTIC_LOD_PHI) - 0.5);
}

inline float StochasticHeightFadeFromMip(float mipLevel)
{
	return saturate((mipLevel - HEIGHT_BLEND_FADE_MIP_START) / HEIGHT_BLEND_FADE_MIP_RANGE);
}

// Contrast sharpening on barycentric weights.
inline float StochasticContrastWeight(float weight)
{
	float w = saturate(weight);
	float w2 = w * w;
	float w4 = w2 * w2;
	return w4 * w4;
}

// Shared height-vs-stochastic weighting for diffuse + parallax (single IR body vs duplicating below).
inline float StochasticHeightBlendInfluence(float mipLevel)
{
	float heightFade = StochasticHeightFadeFromMip(mipLevel);
	return HEIGHT_INFLUENCE * (1.0 - heightFade);
}

// secondSampleScale fades the second tap's contribution to zero near the single-sample cutoff so the
// branch boundary is continuous (no pop). At scale 0 the result is exactly s1.
inline float4 StochasticBlendTwoSamples(float mipLevel, float4 s1, float4 s2, float3 weights, float blendFactor1, float blendFactor2, float secondSampleScale)
{
	float w1 = StochasticContrastWeight(weights.x);
	float w2 = StochasticContrastWeight(weights.y) * secondSampleScale;
	float hi = StochasticHeightBlendInfluence(mipLevel);
	w1 *= (1.0 + hi * blendFactor1);
	w2 *= (1.0 + hi * blendFactor2);
	float denom = max(w1 + w2, 1e-8);
	return lerp(s2, s1, w1 / denom);
}

// LOD terrain stochastic sampling — 2 SampleBias, fixed blend (pass jitter from StochasticSampleLODJitter(screenNoise)).
inline float4 StochasticSampleLOD(float2 jitter, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD)
{
	float4 result = tex.SampleBias(samp, uv, SharedData::MipBias);
	if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix) {
		float2 j1 = (offsetsLOD.offset1 + jitter) * 0.01;
		float2 j2 = (offsetsLOD.offset2 + float2(jitter.y, -jitter.x)) * 0.01;
		float4 s1 = tex.SampleBias(samp, uv + j1, SharedData::MipBias);
		float4 s2 = tex.SampleBias(samp, uv + j2, SharedData::MipBias);
		result = lerp(s2, s1, offsetsLOD.weights.x);
	}
	return result;
}

// 2-sample height-blended stochastic sampling. Uses one shared gradient (SampleGrad) for both taps so
// filtering stays consistent and anisotropy is preserved; the second tap fades out with distance.
// Sorting in ComputeStochasticOffsets guarantees offset1/offset2 are the two
// highest-weight barycentric vertices, so dropping offset3 loses minimal quality.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float extraLandMipBias)
{
	// One gradient pair, reused for the mip estimate and both taps (no separate CalculateLevelOfDetail op).
	float2 dUVdx = ddx(uv);
	float2 dUVdy = ddy(uv);

	float2 texDim;
	tex.GetDimensions(texDim.x, texDim.y);
	float2 dxT = dUVdx * texDim;
	float2 dyT = dUVdy * texDim;
	float mipLevel = max(0.5 * log2(max(dot(dxT, dxT), dot(dyT, dyT))), 0.0) + SharedData::MipBias + extraLandMipBias;

	float secondSampleFade = saturate((TV_SINGLE_SAMPLE_MIP_START - mipLevel) * TV_SINGLE_SAMPLE_FADE_RCP);

	float4 s1 = tex.SampleGrad(samp, uv + offsets.offset1, dUVdx, dUVdy);
	float4 result = s1;
	if (secondSampleFade > 0.0) {
		float4 s2 = tex.SampleGrad(samp, uv + offsets.offset2, dUVdx, dUVdy);
		float h1 = lerp(dot(s1.rgb, LUMINANCE_WEIGHTS), s1.a, step(0.001, s1.a));
		float h2 = lerp(dot(s2.rgb, LUMINANCE_WEIGHTS), s2.a, step(0.001, s2.a));
		result = StochasticBlendTwoSamples(mipLevel, s1, s2, offsets.weights, h1, h2, secondSampleFade);
	}
	return result;
}

// 2-sample parallax/height sampling. MUST use the same dual-tap stochastic blend (same offsets/weights)
// as StochasticEffect so the displaced height field stays aligned with the de-tiled albedo/normal —
// otherwise parallax is computed against a different surface than the one being shaded.
// Deliberately BRANCHLESS: this is inlined dozens of times across the unrolled ray-march / secant /
// soft-shadow paths, and it's the duplicated control flow (not the second fetch) that explodes FXC
// compile time. The second tap fades with distance via secondSampleScale, mirroring StochasticEffect.
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	float secondSampleFade = saturate((TV_SINGLE_SAMPLE_MIP_START - mipLevel) * TV_SINGLE_SAMPLE_FADE_RCP);
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	return StochasticBlendTwoSamples(mipLevel, s1, s2, offsets.weights, s1.a, s2.a, secondSampleFade);
}

inline float4 SampleTerrain(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float extraLandMipBias)
{
	return StochasticEffect(tex, samp, uv, offsets, extraLandMipBias);
}

#endif  // TERRAIN_VARIATION_HLSLI