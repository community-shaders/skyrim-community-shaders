#ifndef __SNOW_FIELDS_DEPENDENCY_HLSL__
#define __SNOW_FIELDS_DEPENDENCY_HLSL__

#include "SnowDeformation/SnowCommon.hlsli"
#include "SnowDeformation/SnowNoise.hlsli"

#if defined(SNOW_NOISE_TABLE)
Texture2D<float> SnowNoiseHash : register(t109);
Texture2DArray<float2> SnowTrenchDetailN : register(t112);
Texture2DArray<float2> SnowTrenchDetailH : register(t113);

float SnowDetailNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	return SnowNoiseHash.SampleLevel(SnowSampler, (i + f * f * (3.0 - 2.0 * f) + 0.5) / kNoiseTorusCells, 0.0);
}
#else
float SnowDetailNoise(float2 p)
{
	return ShapeNoiseTorus(p, kNoiseTorusCells);
}
#endif

static const float kTrenchDetailLatticePeriod = 64.0;

#ifdef SNOW_FIELD_DIMS
#	define SNOW_DEFORM_DIMS(d) float2 d = FieldDims.xx
#	define SNOW_BERM_DIMS(d) float2 d = FieldDims.yy
#	define SNOW_EXCLUSION_DIMS(d) float2 d = FieldDims.ww
#else
#	define SNOW_DEFORM_DIMS(d) \
		float2 d;               \
		DeformationMap.GetDimensions(d.x, d.y)
#	define SNOW_BERM_DIMS(d) \
		float2 d;             \
		BermFieldMap.GetDimensions(d.x, d.y)
#	define SNOW_EXCLUSION_DIMS(d) \
		float2 d;                  \
		ExclusionFieldMap.GetDimensions(d.x, d.y)
#endif

float ShellEdgeFade(float2 gridLocal)
{
	float2 delta = abs(gridLocal - WarpedHalfSpan);
	float fade = saturate((WarpedHalfSpan - max(delta.x, delta.y)) / 2048.0);
	[flatten] if (SeamRampInv > 0.0)
	{
		float2 worldXY = GridOrigin + gridLocal;
		float inside = min(min(worldXY.x - SeamBounds.x, worldXY.y - SeamBounds.y),
			min(SeamBounds.z - worldXY.x, SeamBounds.w - worldXY.y));
		fade = min(fade, saturate(inside * SeamRampInv));
	}
	return fade;
}

float ShellEdgeDepth(float a_depth, float a_fade)
{
	return min(a_depth, a_depth * a_fade);
}

float BermShape(float bermDeform)
{
	return smoothstep(0.02, 0.32, bermDeform);
}

float BermGradStep()
{
	SNOW_DEFORM_DIMS(dims);
	return min(4.0, max(4.0 * kBermReach, 0.5 / (DeformInvWorldSize * dims.x)));
}

static const float kWallBlurReach = 12.0;

float2 BermFieldBaked(float2 gridLocal)
{
	float2 uv = (GridToDeformOffset + gridLocal) * DeformInvWorldSize;
	if (any(uv < 0.0) || any(uv > 1.0))
		return 0.0;

	SNOW_BERM_DIMS(dims);
	float2 t = clamp(uv * dims - 0.5, 0.0, dims - 1.001);
#ifdef SNOW_DEFORM_SAMPLER
	float2 v = BermFieldMap.SampleLevel(SnowSampler, (t + 0.5 + float2(DeformMapOrigin)) / dims, 0.0);
#else
	int2 t0 = (int2)t;
	float2 f = t - t0;
	int2 t1 = min(t0 + 1, int2(dims) - 1);

	float2 s00 = BermFieldMap.Load(DeformTexel(int2(t0.x, t0.y), int2(dims)));
	float2 s10 = BermFieldMap.Load(DeformTexel(int2(t1.x, t0.y), int2(dims)));
	float2 s01 = BermFieldMap.Load(DeformTexel(int2(t0.x, t1.y), int2(dims)));
	float2 s11 = BermFieldMap.Load(DeformTexel(int2(t1.x, t1.y), int2(dims)));

	float2 v = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
#endif
	return v;
}

static const float kSunMarchDist[5] = { 12.0, 32.0, 90.0, 300.0, 1000.0 };
static const float kSunNearTaps[4] = { 0.45, 0.9, 1.45, 2.05 };
static const float kSunEnvelopeShift = 1.5;
static const uint kSunEnvelopeSteps = 38;

float SunEnvelopeRun(uint a_k)
{
	return a_k <= 20u ? 0.5 * a_k : 10.0 + (float)(a_k - 20u);
}

#if defined(PSHADER) || defined(SNOW_MARCH_SUN_CS)
Texture2D<float> MarchHeightGround : register(t106);
Texture2D<float> MarchHeightFar : register(t108);
Texture2D<float> MarchSunEnvelope : register(t100);

float4 MarchCacheTexels(Texture2D<float> a_map, float2 a_gridLocal, out float2 a_f)
{
	SNOW_DEFORM_DIMS(dims);
	const float2 t = (GridToDeformOffset + a_gridLocal) * DeformInvWorldSize * dims - 0.5;
	const float2 t0 = floor(t);
	a_f = t - t0;
	return a_map.GatherRed(SnowSampler, (t0 + float2(DeformMapOrigin) + 1.0) / dims).wzxy;
}

float MarchCacheBilinear(float4 a_s, float2 a_f)
{
	return lerp(lerp(a_s.x, a_s.y, a_f.x), lerp(a_s.z, a_s.w, a_f.x), a_f.y);
}

bool MarchCacheCovers(float2 a_gridLocal, float2 a_stepDir, float a_reach)
{
	SNOW_DEFORM_DIMS(dims);
	const float2 margin = 1.5 / dims;
	const float2 uv0 = (GridToDeformOffset + a_gridLocal) * DeformInvWorldSize;
	const float2 uv1 = uv0 + a_stepDir * (a_reach * DeformInvWorldSize);
	return all(min(uv0, uv1) >= margin) && all(max(uv0, uv1) <= 1.0 - margin);
}

bool MarchFarCovers(float2 a_gridLocal, float2 a_stepDir, float a_reach)
{
	const float2 t0 = (FineWindow.xy + a_gridLocal) / FineWindow.w;
	const float2 t1 = t0 + a_stepDir * (a_reach / FineWindow.w);
	return FineWindow.z > 0.5 && all(min(t0, t1) >= 1.0) && all(max(t0, t1) <= FineWindow.z - 2.0);
}

float MarchHeightNear(float2 a_gridLocal)
{
	float2 f;
	return MarchCacheBilinear(MarchCacheTexels(MarchHeightGround, a_gridLocal, f), f);
}

float MarchHeightFarAt(float2 a_gridLocal)
{
	const float2 t = (FineWindow.xy + a_gridLocal) / FineWindow.w;
	const float2 t0 = floor(t);
	return MarchCacheBilinear(MarchHeightFar.GatherRed(SnowSampler, (t0 + 1.0) / FineWindow.z).wzxy, t - t0);
}

void MarchCachedHeights(float2 a_gridLocal, float2 a_stepDir, bool a_near, out float a_h[6])
{
	[branch] if (a_near)
	{
		a_h[0] = MarchHeightNear(a_gridLocal);
		[unroll] for (uint i = 0; i < 5; i++)
			a_h[i + 1] = MarchHeightNear(a_gridLocal + a_stepDir * kSunMarchDist[i]);
	}
	else
	{
		a_h[0] = MarchHeightFarAt(a_gridLocal);
		[unroll] for (uint i = 0; i < 5; i++)
			a_h[i + 1] = MarchHeightFarAt(a_gridLocal + a_stepDir * kSunMarchDist[i]);
	}
}

float2 MarchNearHorizon(float2 a_gridLocal, float2 a_stepDir, float a_sunTan, float a_ref)
{
	SNOW_DEFORM_DIMS(dims);
	const float texel = 1.0 / (DeformInvWorldSize * dims.x);
	float horizonTan = -10.0;
	[unroll] for (uint i = 0; i < 4; i++)
	{
		const float d = kSunNearTaps[i] * texel;
		horizonTan = max(horizonTan, (MarchHeightNear(a_gridLocal + a_stepDir * d) - a_ref - 1.0) / d);
	}
	[unroll] for (uint j = 3; j < 5; j++)
		horizonTan = max(horizonTan, (MarchHeightNear(a_gridLocal + a_stepDir * kSunMarchDist[j]) - a_ref - 1.0) / kSunMarchDist[j]);
	const float shift = kSunEnvelopeShift * texel;
	float2 f;
	const float envelope = MarchCacheBilinear(MarchCacheTexels(MarchSunEnvelope, a_gridLocal + a_stepDir * shift, f), f) - shift * a_sunTan;
	return float2(horizonTan, envelope - a_ref - 1.0);
}

float MarchNearLight(float2 a_horizon, float a_sunTan, float a_soft, float a_softUnits)
{
	return min(smoothstep(-1.5 * a_soft, 1.5 * a_soft, a_sunTan - a_horizon.x), smoothstep(-a_softUnits, a_softUnits, -a_horizon.y));
}
#endif

float SampleDeformationBilinear(float2 t, float2 dims)
{
	t = clamp(t, 0.0, dims - 1.001);
#ifdef SNOW_DEFORM_SAMPLER
	return saturate(DeformationMap.SampleLevel(SnowSampler, (t + 0.5 + float2(DeformMapOrigin)) / dims, 0.0).x);
#else
	int2 t0 = (int2)t;
	float2 f = t - t0;
	int2 t1 = min(t0 + 1, int2(dims) - 1);

	float s00 = DeformationMap.Load(DeformTexel(int2(t0.x, t0.y), int2(dims))).x;
	float s10 = DeformationMap.Load(DeformTexel(int2(t1.x, t0.y), int2(dims))).x;
	float s01 = DeformationMap.Load(DeformTexel(int2(t0.x, t1.y), int2(dims))).x;
	float s11 = DeformationMap.Load(DeformTexel(int2(t1.x, t1.y), int2(dims))).x;

	return saturate(lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y));
#endif
}

float SampleDeformationSmooth(float2 gridLocal)
{
	float2 uv = (GridToDeformOffset + gridLocal) * DeformInvWorldSize;
	if (any(uv < 0.0) || any(uv > 1.0))
		return 0.0;

	SNOW_DEFORM_DIMS(dims);
	SNOW_DEFORM_BSPLINE(SampleDeformationBilinear, uv, dims);
	return bspline;
}

bool ShellUntouched(float2 gridLocal)
{
	float2 uv = (GridToDeformOffset + gridLocal) * DeformInvWorldSize;
	float2 pad = 8.0 * DeformInvWorldSize;
	bool farOutside = any(uv < -pad) || any(uv > 1.0 + pad);
	SNOW_DEFORM_DIMS(dims);
	int2 logical = clamp(int2(uv * dims), 0, int2(dims) - 1);
	bool zeroTile = UntouchedTiles.Load(int3(((logical + DeformMapOrigin) & (int2(dims) - 1)) >> 3, 0)) != 0;
	bool inside = all(uv >= 0.0) && all(uv <= 1.0);
	return BermBakeActive > 0.5 && (farOutside || (inside && zeroTile));
}

static const float kTrenchDetailTile = 64.0;
static const float kTrenchDetailSpan = 6.0;
static const float kTrenchDetailReach = 2.75;
static const float kTrenchDetailPowder = 0.0;
static const float kTrenchDetailImpact = 1.0;
static const float kImpactReliefBoost = 0.5;

struct TrenchDetailTaps
{
	float4 uv;
	float4 turn;
	float w;
};

float2 TrenchDetailTurn(float2 a_offset)
{
	const float a = 6.2831853 * frac(dot(a_offset, float2(17.13, 31.71)));
	return float2(cos(a), sin(a));
}

float2 TrenchDetailRotate(float2 v, float2 cs)
{
	return float2(cs.x * v.x - cs.y * v.y, cs.y * v.x + cs.x * v.y);
}

TrenchDetailTaps TrenchDetailTapsAt(float2 worldXY)
{
	const float2 uv = worldXY / kTrenchDetailTile;
#if defined(SNOW_TERRAIN_VARIATION)
	float2 cell1, cell2;
	const StochasticOffsets o = ComputeStochasticOffsetsScaledPeriodic(uv, MESH_SCALE, kTrenchDetailLatticePeriod, cell1, cell2);
	TrenchDetailTaps t;
	t.turn = float4(TrenchDetailTurn(o.offset1), TrenchDetailTurn(o.offset2));
	t.uv = float4(TrenchDetailRotate(uv - StochasticCellUV(cell1, MESH_SCALE), t.turn.xy) + o.offset1,
		TrenchDetailRotate(uv - StochasticCellUV(cell2, MESH_SCALE), t.turn.zw) + o.offset2);
	t.w = o.tap1Weight;
#else
	// Without Terrain Variation installed, one plain tile: no turn, all weight on the first tap
	TrenchDetailTaps t;
	t.turn = float4(1.0, 0.0, 1.0, 0.0);
	t.uv = uv.xyxy;
	t.w = 1.0;
#endif
	return t;
}

float4 TrenchDetailStep(TrenchDetailTaps a_t, float2 a_d)
{
	return float4(TrenchDetailRotate(a_d, a_t.turn.xy), TrenchDetailRotate(a_d, a_t.turn.zw)) / kTrenchDetailTile;
}

float TrenchDetailLod(float2 a_dx, float2 a_dy)
{
	return max(0.5 * log2(max(min(dot(a_dx, a_dx), dot(a_dy, a_dy)), 1e-12)) + log2(1024.0 / kTrenchDetailTile) + SharedData::MipBias, 0.0);
}

float TrenchDetailMask(float a_carve, float2 a_bermPair, float a_coverage)
{
	const float f = max(0.85 * a_carve, max(a_bermPair.y, 0.8 * a_bermPair.x));
	const float cut = 0.85 - 0.325 * a_coverage;
	return smoothstep(cut - 0.4, cut, f) * smoothstep(0.0, 0.15, f);
}

float TrenchSpoilMask(float a_energy)
{
	return smoothstep(0.3, 0.45, a_energy);
}

float TrenchDetailHeight01(float2 a_uv, float a_lod, float a_impact)
{
#if defined(SNOW_NOISE_TABLE)
	float h = SnowTrenchDetailH.SampleLevel(SnowSampler, float3(a_uv, kTrenchDetailPowder), a_lod).x;
	[branch] if (a_impact > 0.001)
		h = lerp(h, SnowTrenchDetailH.SampleLevel(SnowSampler, float3(a_uv, kTrenchDetailImpact), a_lod).x, a_impact);
	return h;
#else
	return 0.5;
#endif
}

float TrenchDetailHeight(float4 a_uv, float a_w, float a_lod, float a_impact)
{
	const float h1 = TrenchDetailHeight01(a_uv.xy, a_lod, a_impact);
	const float h2 = TrenchDetailHeight01(a_uv.zw, a_lod, a_impact);
	return (lerp(h2, h1, a_w) - 0.5) * kTrenchDetailSpan;
}

float2 TrenchDetailGrad01(float2 a_uv, float a_lod, float a_impact)
{
#if defined(SNOW_NOISE_TABLE)
	float2 n = SnowTrenchDetailN.SampleLevel(SnowSampler, float3(a_uv, kTrenchDetailPowder), a_lod) * 2.0 - 1.0;
	float2 g = -n / max(sqrt(saturate(1.0 - dot(n, n))), 0.25);
	[branch] if (a_impact > 0.001)
	{
		n = SnowTrenchDetailN.SampleLevel(SnowSampler, float3(a_uv, kTrenchDetailImpact), a_lod) * 2.0 - 1.0;
		g = lerp(g, -n / max(sqrt(saturate(1.0 - dot(n, n))), 0.25), a_impact);
	}
	return g;
#else
	return 0.0;
#endif
}

float2 TrenchDetailGrad(TrenchDetailTaps a_t, float a_lod, float a_impact)
{
	const float2 g1 = TrenchDetailRotate(TrenchDetailGrad01(a_t.uv.xy, a_lod, a_impact), a_t.turn.xy * float2(1.0, -1.0));
	const float2 g2 = TrenchDetailRotate(TrenchDetailGrad01(a_t.uv.zw, a_lod, a_impact), a_t.turn.zw * float2(1.0, -1.0));
	return lerp(g2, g1, a_t.w);
}

float TrenchDetailParallaxT(float3 dir, float2 g, TrenchDetailTaps a_t, float4 a_duv, float3 a_detail, float a_impact, float a_noise)
{
	const float rise = dir.z - dot(dir.xy, g);
	const float hMax = a_detail.x * kTrenchDetailReach;
	float t = 0.0;
	[branch] if (rise <= -0.02)
	{
		const float t0 = hMax / rise;
		const float span = -2.0 * hMax / rise;
		const float steps = clamp(ceil(span * length(dir.xy) / a_detail.z), 6.0, 40.0);
		const float dt = span / steps;
		float prevT = t0;
		float prevGap = hMax - a_detail.x * TrenchDetailHeight(a_t.uv + a_duv * t0, a_t.w, a_detail.y, a_impact);
		t = t0 + span;
		[loop] for (float i = 0.0; i < steps; i += 1.0)
		{
			const float ti = t0 + dt * (i + a_noise);
			const float gap = rise * ti - a_detail.x * TrenchDetailHeight(a_t.uv + a_duv * ti, a_t.w, a_detail.y, a_impact);
			[branch] if (gap <= 0.0)
			{
				t = lerp(prevT, ti, prevGap / max(prevGap - gap, 1e-6));
				break;
			}
			prevT = ti;
			prevGap = gap;
		}
	}
	return t;
}

float TrenchDetailSunShadow(TrenchDetailTaps a_t, float a_h, float3 L, float2 g, float3 a_detail, float a_impact, float a_noise)
{
	const float hMax = a_detail.x * kTrenchDetailReach;
	const float climb = L.z - dot(L.xy, g);
	float lit = 1.0;
	[branch] if (climb > 0.01 && a_h < hMax)
	{
		const float reach = min((hMax - a_h) / climb, 32.0);
		const float4 duv = TrenchDetailStep(a_t, L.xy);
		float occlusion = 0.0;
		[loop] for (float i = 0.0; i < 10.0; i += 1.0)
		{
			const float d = reach * (i + 0.1 + 0.9 * a_noise) / 10.0;
			const float over = a_detail.x * TrenchDetailHeight(a_t.uv + duv * d, a_t.w, a_detail.y, a_impact) - (a_h + climb * d);
			occlusion = max(occlusion, over / (0.2 * d + 0.05));
		}
		lit = 1.0 - saturate(occlusion * a_detail.z);
	}
	return lit;
}

#define SNOW_DEFORM_BILINEAR2(v, gridLocal)                                      \
	float2 uv = (GridToDeformOffset + gridLocal) * DeformInvWorldSize;           \
	if (any(uv < 0.0) || any(uv > 1.0))                                          \
		return 0.0;                                                              \
	SNOW_DEFORM_DIMS(dims);                                                      \
	float2 t = clamp(uv * dims - 0.5, 0.0, dims.x - 1.001);                      \
	int2 t0 = (int2)t;                                                           \
	float2 f = t - t0;                                                           \
	int2 t1 = min(t0 + 1, int2(dims) - 1);                                       \
	float2 s00 = DeformationMap.Load(DeformTexel(int2(t0.x, t0.y), int2(dims))); \
	float2 s10 = DeformationMap.Load(DeformTexel(int2(t1.x, t0.y), int2(dims))); \
	float2 s01 = DeformationMap.Load(DeformTexel(int2(t0.x, t1.y), int2(dims))); \
	float2 s11 = DeformationMap.Load(DeformTexel(int2(t1.x, t1.y), int2(dims))); \
	float2 v = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y)

float SampleDetailSpoil(float2 gridLocal)
{
	SNOW_DEFORM_BILINEAR2(v, gridLocal);
	return saturate(v.y);
}

Texture2D<float> BodyReachMax : register(t56);

float TrampleDetailScale(float2 worldXY, float deformation)
{
	float scale = 1.0;
	const float2 uv = (GridToDeformOffset + worldXY - GridOrigin) * DeformInvWorldSize;
	[branch] if (deformation > 0.001 && all(uv >= 0.0) && all(uv <= 1.0))
	{
		SNOW_DEFORM_DIMS(dims);
		const float2 t = clamp(uv * dims - 0.5, 0.0, dims.x - 1.001);
		const int2 t0 = (int2)t;
		const float2 f = t - t0;
		const int2 t1 = min(t0 + 1, int2(dims) - 1);
		float4 packed;
		[unroll] for (int i = 0; i < 4; i++)
		{
			const int3 texel = DeformTexel(int2((i & 1) ? t1.x : t0.x, (i & 2) ? t1.y : t0.y), int2(dims));
			const float carve = saturate(DeformationMap.Load(texel).x);
			const float reach = BodyReachMax.Load(texel);
			const float layer = reach > 0.0 ? reach : 1.0;
			const float pressed = carve / layer;
			packed[i] = saturate((pressed - BodyPrint) / max(1.0 - BodyPrint, 1e-3)) * smoothstep(0.05, 0.5, layer);
		}
		scale = lerp(1.0, kTrampledFlattening, lerp(lerp(packed.x, packed.y, f.x), lerp(packed.z, packed.w, f.x), f.y));
	}
	return scale;
}

#ifdef PSHADER

float SnowPressed(float a_carve, float a_melt)
{
	return smoothstep(0.1, 0.5, a_carve) * (1.0 - smoothstep(0.1, 0.4, a_melt));
}

// A print in dry cold snow at the snow climate's default strength (0.75) keeps a quarter of the wet look.
static const float kDryWetPrint = 0.25;

float SnowWetPrint(inout float3 a_albedo, float a_carve, float a_melt, float a_depth)
{
	float roughCut = 0.0;
	const float wetPress = SnowPressed(a_carve, a_melt);
	[branch] if (wetPress > 0.001 && a_depth < 6.0)
	{
		const float wetPrint = wetPress * (1.0 - smoothstep(1.5, 6.0, a_depth)) * kDryWetPrint;
		a_albedo = lerp(a_albedo, a_albedo * float3(0.76, 0.77, 0.80), wetPrint);
		roughCut = wetPrint * 0.24;
	}
	return roughCut;
}
#endif

float2 SampleExclusionField(float2 worldXY)
{
	float2 result = 0.0;
	[branch] if (ExclusionFieldWindow.w > 0.5)
	{
		float2 local = (worldXY - ExclusionFieldWindow.xy) * ExclusionFieldWindow.z;
		[branch] if (all(abs(local) < 0.995))
		{
			SNOW_EXCLUSION_DIMS(dims);
			float2 uv = local * 0.5 + 0.5;
			float2 t = clamp(uv * dims - 0.5, 0.0, dims - 1.001);
			int2 t0 = (int2)t;
			float2 f = t - t0;
			int2 t1 = min(t0 + 1, int2(dims) - 1);

			float2 s00 = ExclusionFieldMap.Load(int3(t0.x, t0.y, 0));
			float2 s10 = ExclusionFieldMap.Load(int3(t1.x, t0.y, 0));
			float2 s01 = ExclusionFieldMap.Load(int3(t0.x, t1.y, 0));
			float2 s11 = ExclusionFieldMap.Load(int3(t1.x, t1.y, 0));

			result = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
		}
	}
	return result;
}

static const float kFireMeltFloor = 1.0;

float Undulation(float2 worldXY)
{
	return UndulationNorm(worldXY) * kUndulationAmp;
}

float3 UndulationBakedHG(float2 worldXY)
{
	float2 uv = (worldXY - UndulationFieldWindow.xy) * UndulationFieldWindow.z * 0.5 + 0.5;
	float2 dims;
	UndulationFieldMap.GetDimensions(dims.x, dims.y);
	float2 t = clamp(uv * dims - 0.5, 0.0, dims - 1.001);
	int2 t0 = (int2)t;
	float2 f = t - t0;
	float3 s00 = UndulationFieldMap.Load(int3(t0, 0)).xyz;
	float3 s10 = UndulationFieldMap.Load(int3(t0 + int2(1, 0), 0)).xyz;
	float3 s01 = UndulationFieldMap.Load(int3(t0 + int2(0, 1), 0)).xyz;
	float3 s11 = UndulationFieldMap.Load(int3(t0 + int2(1, 1), 0)).xyz;
	return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}

bool UndulationBakedCovers(float2 worldXY)
{
	return UndulationFieldWindow.w > 0.5 &&
	       max(abs(worldXY.x - UndulationFieldWindow.x), abs(worldXY.y - UndulationFieldWindow.y)) * UndulationFieldWindow.z < 0.999;
}

float UndulationSampled(float2 worldXY)
{
	float result;
	[branch] if (UndulationBakedCovers(worldXY))
		result = UndulationBakedHG(worldXY).x * kUndulationAmp;
	else result = Undulation(worldXY);
	return result;
}

float2 UndulationGradSampled(float2 worldXY)
{
	float2 result;
	[branch] if (UndulationBakedCovers(worldXY))
		result = UndulationBakedHG(worldXY).yz * kUndulationAmp;
	else
	{
		const float uStep = kUndulationGradStep;
		result = float2(
					 Undulation(worldXY + float2(uStep, 0.0)) - Undulation(worldXY - float2(uStep, 0.0)),
					 Undulation(worldXY + float2(0.0, uStep)) - Undulation(worldXY - float2(0.0, uStep))) /
		         (2.0 * uStep);
	}
	return result;
}

#define CARVE_PROFILE_SHAPE(profile, d, depthT, uncarvedDepth)                  \
	float soft = d * d * d * (d * (d * 6.0 - 15.0) + 10.0);                     \
	d = lerp(soft, d, depthT);                                                  \
	float profile = uncarvedDepth * (1.0 - d);                                  \
	float hump = smoothstep(0.02, 0.10, d) * (1.0 - smoothstep(0.10, 0.35, d)); \
	profile += kRimLip * uncarvedDepth * hump * depthT;

float CarveProfile(float deformation, float uncarvedDepth, float2 worldXY)
{
	float depthT = smoothstep(6.0, 18.0, uncarvedDepth);
	float d = saturate(deformation);

	float rimBand = smoothstep(0.02, 0.12, d) * (1.0 - smoothstep(0.30, 0.55, d));
	[branch] if (rimBand > 0.0 && depthT > 0.0)
	{
		float notch = (SnowDetailNoise(worldXY / 37.0) - 0.5) * 0.8 +
		              (SnowDetailNoise(worldXY / 8.0) - 0.5) * 1.2;
		d = saturate(d + notch * kRimTeeth * 0.35 * rimBand * depthT);
	}

	CARVE_PROFILE_SHAPE(profile, d, depthT, uncarvedDepth)
	return profile;
}

float CarveProfileNoTeeth(float deformation, float uncarvedDepth)
{
	float depthT = smoothstep(6.0, 18.0, uncarvedDepth);
	float d = saturate(deformation);
	CARVE_PROFILE_SHAPE(profile, d, depthT, uncarvedDepth)
	return profile;
}

float BermDepthGate(float depth)
{
	return smoothstep(3.0, 12.0, depth);
}

#if defined(PSHADER)
float DecodeTerrainVertexAO(float4 texel)
{
	return (texel.x > -50000.0 && texel.w < 0.5) ? saturate(texel.w * (1.0 / 0.499)) : 1.0;
}

float SampleTerrainVertexAO(float2 gridLocal)
{
	float2 t = clamp((GridToTerrainOffset + gridLocal) / TerrainTexelSize, 0.0, (float)(TerrainDim - 1) - 0.001);
	int2 t0 = (int2)t;
	float2 f = t - t0;
	int2 t1 = min(t0 + 1, int2(TerrainDim, TerrainDim) - 1);
	float s00 = DecodeTerrainVertexAO(TerrainWindow.Load(int3(t0.x, t0.y, 0)));
	float s10 = DecodeTerrainVertexAO(TerrainWindow.Load(int3(t1.x, t0.y, 0)));
	float s01 = DecodeTerrainVertexAO(TerrainWindow.Load(int3(t0.x, t1.y, 0)));
	float s11 = DecodeTerrainVertexAO(TerrainWindow.Load(int3(t1.x, t1.y, 0)));
	return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}
#endif

static const float kHeightBlendMult = 8.0;
static const float kHeightBlendPower = 2.0;

float SnowHeightBlendSharpness(float viewDist)
{
	float nearBlendToFar = smoothstep(1024.0 * 1024.0, 2048.0 * 2048.0, viewDist * viewDist);
	float blendFactor = sqrt(saturate(1.0 - nearBlendToFar));
	return 1.0 + blendFactor * kHeightBlendPower;
}

float SnowHeightBlend(float w, float hSnow, float hOther, float heightBlend)
{
	if (heightBlend <= 1.0 || abs(hSnow - hOther) <= 1e-3)
		return w;
	float logHeightBlend = log2(heightBlend);
	float wSnow = min(100, exp2(heightBlend * (log2(abs(w)) + kHeightBlendMult * hSnow * logHeightBlend)));
	float wOther = min(100, exp2(heightBlend * (log2(abs(1.0 - w)) + kHeightBlendMult * hOther * logHeightBlend)));
	return wSnow * rcp(max(wSnow + wOther, 1e-6));
}

float SnowHeightBlendOneSided(float w, float hSnow, float heightBlend)
{
	return SnowHeightBlend(w, hSnow, 1.0 - w, heightBlend);
}

#endif
