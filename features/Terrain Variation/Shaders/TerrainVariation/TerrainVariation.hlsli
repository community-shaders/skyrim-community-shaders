// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/ColorManagement.hlsli"
#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS AND STRUCTURES --------------------- //
static const float HEIGHT_INFLUENCE = 0.3;  // How much height affects blending (0=pure stochastic, 1=pure height)
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
// Mesh UVs already tile, so one lattice cell maps to one UV tile.
static const float MESH_SCALE = 1.0;
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
// Fade width for c1/c2 rank-swap seams when discarding the third triangle corner.
static const float TAP_SWAP_FADE_WIDTH = 0.1;
// Disables the rank-swap fade near c0/c1 ties so the primary swap stays symmetric.
static const float DOMINANCE_GUARD_WIDTH = 0.02;
static const float STOCHASTIC_LOD_PHI = 1.618;
static const float STOCHASTIC_LOD_BLEND = 0.65;

// Structure to hold stochastic sampling offsets and tap weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	// Normalized weight of tap 1; tap 2 gets the remainder.
	float tap1Weight;
};

struct StochasticCorner
{
	float2 cell;
	float w;
};

// --------------------- COMPUTE FUNCTIONS --------------------- //

// Hash function for stochastic sampling
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

// Value of the top-left pixel of the 2x2 quad, read through fine derivatives (SM5 has no QuadReadLaneAt).
// Exact for integer-valued input, so every pixel of the quad gets the same bits.
inline float4 QuadReadTopLeft(float4 v, float2 quadPixel)
{
	v -= quadPixel.x * ddx_fine(v);
	return v - quadPixel.y * ddy_fine(v);
}

inline float2 QuadReadTopLeft(float2 v, float2 quadPixel)
{
	v -= quadPixel.x * ddx_fine(v);
	return v - quadPixel.y * ddy_fine(v);
}

// Barycentric weight of a lattice corner (hat function of the skewed triangular grid).
// Equals the barycentric inside the corner's six triangles and 0 elsewhere.
inline float StochasticCornerWeight(float2 skewUV, float2 cell)
{
	float2 d = skewUV - cell;
	return saturate(1.0 - max(max(abs(d.x), abs(d.y)), abs(d.x + d.y)));
}

// Two highest barycentric corners of the triangular grid.
// Near c1/c2 ties, fade w2 so the discarded corner cannot pop;
// near c0/c1 ties, disable that fade so the primary swap stays symmetric.
// Contrast exponent is 2, so weights are squared rather than pow()'d.
// Corners are chosen once per 2x2 quad, so each tap's UV offset is constant across the quad and
// Sample/SampleBias get clean implicit derivatives (hardware mip and anisotropic filtering).
// Weights stay per pixel, evaluated against the quad's corners.
inline StochasticOffsets ComputeStochasticOffsetsScaled(float2 uv, float scale, float2 pixelPos)
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

	float2 quadPixel = float2(uint2(pixelPos) & 1u);
	float4 tapCells = QuadReadTopLeft(float4(c0.cell, c1.cell), quadPixel);
	float2 droppedCell = QuadReadTopLeft(c2.cell, quadPixel);

	// Equal to c0.w, c1.w, c2.w on the top-left pixel.
	float b1 = StochasticCornerWeight(skewUV, tapCells.xy);
	float b2 = StochasticCornerWeight(skewUV, tapCells.zw);
	float bDropped = StochasticCornerWeight(skewUV, droppedCell);

	// The fade applies to whichever tap is weaker here, which can differ from the top-left pixel.
	float hi = max(b1, b2);
	float lo = min(b1, b2);
	float rankFade = smoothstep(0.0, TAP_SWAP_FADE_WIDTH, lo - bDropped);
	float dominanceGuard = smoothstep(0.0, DOMINANCE_GUARD_WIDTH, hi - lo);
	float fade = lerp(1.0, rankFade, dominanceGuard);
	float w1 = b1 * b1 * (b1 < b2 ? fade : 1.0);
	float w2 = b2 * b2 * (b1 < b2 ? 1.0 : fade);

	StochasticOffsets o;
	o.offset1 = hash2D2D(tapCells.xy);
	o.offset2 = hash2D2D(tapCells.zw);
	o.tap1Weight = w1 * rcp(max(w1 + w2, 1e-8));
	return o;
}

// pixelPos is SV_Position.xy; it selects the pixel's slot in its 2x2 quad.
inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV, float2 pixelPos)
{
	return ComputeStochasticOffsetsScaled(landscapeUV, WORLD_SCALE, pixelPos);
}

inline StochasticOffsets ComputeStochasticOffsetsMesh(float2 meshUV, float2 pixelPos)
{
	return ComputeStochasticOffsetsScaled(meshUV, MESH_SCALE, pixelPos);
}

// --------------------- STOCHASTIC SAMPLING FUNCTIONS --------------------- //

// Height-aware blend of two stochastic taps. At tap1Weight == 1 returns s1.
inline float4 StochasticBlendTwoSamples(float4 s1, float4 s2, float tap1Weight, float blendFactor1, float blendFactor2)
{
	float w1 = tap1Weight * (1.0 + HEIGHT_INFLUENCE * blendFactor1);
	float w2 = (1.0 - tap1Weight) * (1.0 + HEIGHT_INFLUENCE * blendFactor2);
	return lerp(s2, s1, w1 * rcp(max(w1 + w2, 1e-8)));
}

// Stochastic sampling function for Terrain LOD & LOD Mask.
inline float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, out float4 sampledColor)
{
	float2 cellID = floor(uv * 255437.0);
	float2 offset1 = hashLOD(cellID) * 0.08;
	float2 offset2 = hashLOD(cellID + 127.0) * 0.08;

	// Cheap pseudo-rotation using simple transforms
	float2 jitter = float2(rnd - 0.5, frac(rnd * STOCHASTIC_LOD_PHI) - 0.5);
	float2 j1 = (offset1 + jitter) * 0.01;
	float2 j2 = (offset2 + float2(jitter.y, -jitter.x)) * 0.01;
	float4 s1 = tex.SampleBias(samp, uv + j1, SharedData::MipBias);
	float4 s2 = tex.SampleBias(samp, uv + j2, SharedData::MipBias);

	sampledColor = lerp(s2, s1, STOCHASTIC_LOD_BLEND);
	s1 = ColorManagement::DiffuseToWorking(s1);
	s2 = ColorManagement::DiffuseToWorking(s2);

	// Simple 2-sample blend weighted toward first sample
	return lerp(s2, s1, STOCHASTIC_LOD_BLEND);
}

// Height-aware blend of two color taps: luminance for RGB data, alpha when available.
inline float4 StochasticBlendColorTaps(float4 s1, float4 s2, StochasticOffsets offsets, bool colorTexture = false, bool linearInput = false)
{
	// Height weights belong to the source data, independent of the working gamut.
	float h1 = lerp(dot(s1.rgb, float3(0.2125, 0.7154, 0.0721)), s1.a, step(0.001, s1.a));
	float h2 = lerp(dot(s2.rgb, float3(0.2125, 0.7154, 0.0721)), s2.a, step(0.001, s2.a));

	if (colorTexture) {
		s1 = ColorManagement::DiffuseToWorking(s1, linearInput);
		s2 = ColorManagement::DiffuseToWorking(s2, linearInput);
	}

	return StochasticBlendTwoSamples(s1, s2, offsets.tap1Weight, h1, h2);
}

// Main stochastic sampling functions. Offsets are constant per 2x2 quad, so the hardware
// derives mip and anisotropic footprint from implicit derivatives as for a plain Sample.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets)
{
	float4 s1 = tex.Sample(samp, uv + offsets.offset1);
	float4 s2 = tex.Sample(samp, uv + offsets.offset2);
	return StochasticBlendColorTaps(s1, s2, offsets);
}

inline float4 StochasticEffectBias(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float mipBias, bool colorTexture = false, bool linearInput = false)
{
	float4 s1 = tex.SampleBias(samp, uv + offsets.offset1, mipBias);
	float4 s2 = tex.SampleBias(samp, uv + offsets.offset2, mipBias);
	return StochasticBlendColorTaps(s1, s2, offsets, colorTexture, linearInput);
}

// Stochastic sampling for parallax/height; same offsets as albedo so height stays aligned.
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	return StochasticBlendTwoSamples(s1, s2, offsets.tap1Weight, s1.a, s2.a);
}

// Height fetch for the mesh ray-march. Blends on the channel the march reads so relief tracks albedo.
inline float StochasticHeightChannel(Texture2D<float4> tex, SamplerState samp, float2 uv, float mipLevel, uint channel, StochasticOffsets offsets)
{
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	float4 blended = StochasticBlendTwoSamples(s1, s2, offsets.tap1Weight, s1[channel], s2[channel]);
	return blended[channel];
}

#endif  // TERRAIN_VARIATION_HLSLI
