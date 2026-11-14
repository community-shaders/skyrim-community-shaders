#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "TerrainVariation/TerrainVariation.hlsli"
#include "SnowDeformation/SnowColor.hlsli"

#ifdef PSHADER
#	include "SnowDeformation/SnowShellPS.hlsli"
#endif

#if defined(PSHADER) && defined(SNOW_SHELL_DEFERRED)
static float2 gDeferredGridDx;
static float2 gDeferredGridDy;
static float gDeferredZDx;
static float gDeferredZDy;
static float gDeferredRampDepth;
#	define SHELL_DEFERRED_REL(a_rel, a_pos, a_w)                                                                                               \
		{                                                                                                                                      \
			const float2 ndc_ = float2((a_pos).x / TessScreen.y - 1.0, 1.0 - (a_pos).y / TessScreen.z);                                        \
			const float3 r0_ = CameraViewProj[0].xyz;                                                                                          \
			const float3 r1_ = CameraViewProj[1].xyz;                                                                                          \
			const float3 r3_ = CameraViewProj[3].xyz;                                                                                          \
			const float3 c0_ = cross(r1_, r3_);                                                                                                \
			const float3 c1_ = cross(r3_, r0_);                                                                                                \
			const float3 c2_ = cross(r0_, r1_);                                                                                                \
			const float3 b_ = float3(ndc_.x * (a_w) - CameraViewProj[0].w, ndc_.y * (a_w) - CameraViewProj[1].w, (a_w) - CameraViewProj[3].w); \
			a_rel = (b_.x * c0_ + b_.y * c1_ + b_.z * c2_) / dot(r0_, c0_);                                                                    \
		}
#endif

#include "SnowDeformation/SnowShellCB.hlsli"

Texture2D<float4> TerrainWindow : register(t0);
Texture2D<float> WaterWindow : register(t27);
Texture2D<float> WaterRawWindow : register(t28);
Texture2D<uint> WaterNear : register(t19);
static const float kWaterSkimDepth = 2.0;
static const float kWaterTexelDepth = 8.0;

float3 WaterBareTexel(float3 texel, int2 t)
{
	float water = WaterWindow.Load(int3(t.x, (int)TerrainDim - 1 - t.y, 0));
	texel.y = texel.x < water - kWaterTexelDepth ? min(texel.y, -8.0) : texel.y;
	return texel;
}
Texture2D<float4> TerrainWindowWet : register(t58);
Texture2D<float> TerrainBare : register(t26);
Texture2D<float> TerrainFine : register(t13);
Texture2D<float4> DeformationMap : register(t1);

Texture2D<float4> SnowDiffuse : register(t2);
Texture2D<float> SceneDepth : register(t3);
Texture2D<float2> ObjectBottoms : register(t5);
Texture2D<float> ObjectTopsRaw : register(t11);
Texture2D<float4> SnowNormalMap : register(t6);
Texture2D<float4> SnowRmaosMap : register(t7);
Texture2D<float4> SnowHeightMap : register(t8);
Texture2D<float3> LandMasksCopy : register(t10);
#include "SnowDeformation/SnowFieldMaps.hlsli"

SamplerState SnowSampler : register(s0);
#if defined(PSHADER) && defined(SNOW_SHELL_DEFERRED)
#	define SNOW_DEFORM_SAMPLER
#	define SNOW_NOISE_TABLE
#endif

#define SNOW_FIELD_DIMS
#include "SnowDeformation/SnowFields.hlsli"

static const float kSnowUVTile = 4096.0 / 24.0;

#include "SnowDeformation/SnowGrid.hlsli"

#ifndef SNOW_SURFACE_ONLY
struct VS_OUTPUT
{
	linear noperspective centroid float4 Position : SV_POSITION;
	float4 CurrentClip : TEXCOORD0;
	float4 PreviousClip : TEXCOORD1;
	float3 WorldPos : TEXCOORD2;
	float2 GridLocal : TEXCOORD3;
	float4 TerrainNormalAlpha : TEXCOORD6;
};
#endif

float TriangulatedHeight(float h00, float h10, float h01, float h11, float2 f)
{
	float triA = f.y <= f.x ?
	                 h00 + (h10 - h00) * f.x + (h11 - h10) * f.y :
	                 h00 + (h01 - h00) * f.y + (h11 - h01) * f.x;
	float triB = (f.x + f.y) <= 1.0 ?
	                 h00 + (h10 - h00) * f.x + (h01 - h00) * f.y :
	                 h11 + (h10 - h11) * (1.0 - f.y) + (h01 - h11) * (1.0 - f.x);
	return max(triA, triB);
}

float LandHeightFine(float2 gridLocal)
{
	float h = -100000.0;
	float2 tf = (FineWindow.xy + gridLocal) / FineWindow.w;
	bool inside = FineWindow.z > 0.5 && all(tf >= 0.0) && all(tf < FineWindow.z - 1.0);
	[branch] if (inside)
	{
		int2 f0 = (int2)tf;
		float2 ff = tf - f0;
		float g00 = TerrainFine.Load(int3(f0, 0));
		float g10 = TerrainFine.Load(int3(f0 + int2(1, 0), 0));
		float g01 = TerrainFine.Load(int3(f0 + int2(0, 1), 0));
		float g11 = TerrainFine.Load(int3(f0 + int2(1, 1), 0));
		float lo = min(min(g00, g10), min(g01, g11));
		bool slash = ((f0.x + f0.y) & 1) == 0;
		float hA = ff.y <= ff.x ? g00 + (g10 - g00) * ff.x + (g11 - g10) * ff.y : g00 + (g01 - g00) * ff.y + (g11 - g01) * ff.x;
		float hB = (ff.x + ff.y) <= 1.0 ? g00 + (g10 - g00) * ff.x + (g01 - g00) * ff.y : g11 + (g10 - g11) * (1.0 - ff.y) + (g01 - g11) * (1.0 - ff.x);
		h = lo > -50000.0 ? (slash ? hA : hB) : -100000.0;
	}
	return h;
}

float ShellBandStep(float2 gridLocal)
{
	float2 u = float2(InverseWarpAxis(gridLocal.x - WarpedHalfSpan), InverseWarpAxis(gridLocal.y - WarpedHalfSpan));
	return GridSpacing * max(WarpBand(abs(u.x)).x, WarpBand(abs(u.y)).x);
}

float3 SampleTerrain(float2 gridLocal)
{
	SHELL_TERRAIN_TEXEL(gridLocal);

	float3 s00 = TerrainWindowWet.Load(int3(t0.x, t0.y, 0)).xyz;
	float3 s10 = TerrainWindowWet.Load(int3(t1.x, t0.y, 0)).xyz;
	float3 s01 = TerrainWindowWet.Load(int3(t0.x, t1.y, 0)).xyz;
	float3 s11 = TerrainWindowWet.Load(int3(t1.x, t1.y, 0)).xyz;

	float3 result = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
	float fine = -100000.0;
	[branch] if (FineWindow.z > 0.5)
		fine = LandHeightFine(gridLocal);
	[branch] if (fine > -50000.0)
		result.x = fine;
	else if (min(min(s00.x, s10.x), min(s01.x, s11.x)) > -50000.0)
		result.x = TriangulatedHeight(s00.x, s10.x, s01.x, s11.x, f);
	result.y = max(result.y, 0.0) * RimStyle.w + min(result.y, 0.0);
	return result;
}

float2 ShellSeamClamp(float2 gridLocal)
{
	[flatten] if (SeamRampInv > 0.0)
		gridLocal = clamp(gridLocal, SeamBounds.xy - GridOrigin, SeamBounds.zw - GridOrigin);
	return gridLocal;
}

float SampleDeformationFast(float2 gridLocal)
{
	float2 uv = (GridToDeformOffset + gridLocal) * DeformInvWorldSize;
	if (any(uv < 0.0) || any(uv > 1.0))
		return 0.0;

	SNOW_DEFORM_DIMS(dims);
	return SampleDeformationBilinear(uv * dims - 0.5, dims);
}

float2 ObjectMapTexel(float2 worldXY, out float2 dims, out bool valid)
{
	float2 local = (worldXY - ObjectHeightCenter) / ObjectHeightHalfExtent;
	valid = all(abs(local) < 0.98);
	dims = FieldDims.zz;
	float2 uv = float2(local.x * 0.5 + 0.5, 0.5 - local.y * 0.5);
	return clamp(uv * dims - 0.5, 0.0, dims.x - 1.001);
}

static const float kBareDepthFloor = -7.9;

#define SHELL_TERRAIN_RANGE(lo, hi)                               \
	float2 tLo = (GridToTerrainOffset + (lo)) / TerrainTexelSize; \
	float2 tHi = (GridToTerrainOffset + (hi)) / TerrainTexelSize; \
	float maxTexel = (float)(TerrainDim - 1);                     \
	int2 i0 = (int2)clamp(floor(tLo), 0.0, maxTexel);             \
	int2 i1 = (int2)clamp(floor(tHi) + 1.0, 0.0, maxTexel);       \
	i1 = min(i1, i0 + 3)

bool ShellTerrainAllBare(float2 lo, float2 hi)
{
	SHELL_TERRAIN_RANGE(lo, hi);

	float maxDepth = -1e9;
	[loop] for (int y = i0.y; y <= i1.y; ++y)
	{
		[loop] for (int x = i0.x; x <= i1.x; ++x)
		{
			float3 t = TerrainWindowWet.Load(int3(x, y, 0)).xyz;
			[branch] if (t.x < -50000.0)
				return false;
			maxDepth = max(maxDepth, t.y + (-8.0) * saturate(1.0 - saturate(t.z)));
		}
	}
	return maxDepth <= kBareDepthFloor;
}

float2 SampleObjectBottom(float2 worldXY)
{
	float2 dims;
	bool valid;
	float2 t = ObjectMapTexel(worldXY, dims, valid);
	if (!valid)
		return float2(0.0, 0.0);
	int2 t0 = (int2)t;
	float2 f = t - t0;
	int2 t1 = min(t0 + 1, int2(dims) - 1);

	float2 s00 = ObjectBottoms.Load(int3(t0.x, t0.y, 0));
	float2 s10 = ObjectBottoms.Load(int3(t1.x, t0.y, 0));
	float2 s01 = ObjectBottoms.Load(int3(t0.x, t1.y, 0));
	float2 s11 = ObjectBottoms.Load(int3(t1.x, t1.y, 0));

	return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}

float2 SampleExclusionMask(float2 worldXY)
{
	float2 nearMask = 0.0;
	[branch] if (ObjectLiftCap > 0.0)
		nearMask = SampleObjectBottom(worldXY);
	return max(nearMask, SampleExclusionField(worldXY));
}

float4 BermFieldPair(float2 gridLocal)
{
	return BermFieldBaked(gridLocal);
}

float BermField(float2 gridLocal)
{
	return BermFieldPair(gridLocal).x;
}

float2 SampleWaterHeight(float2 gridLocal)
{
	SHELL_TERRAIN_TEXEL(gridLocal);
	int r0 = (int)TerrainDim - 1 - t0.y;
	int r1 = (int)TerrainDim - 1 - t1.y;
	float w00 = WaterRawWindow.Load(int3(t0.x, r0, 0));
	float w10 = WaterRawWindow.Load(int3(t1.x, r0, 0));
	float w01 = WaterRawWindow.Load(int3(t0.x, r1, 0));
	float w11 = WaterRawWindow.Load(int3(t1.x, r1, 0));
	float4 present = step(-50000.0, float4(w00, w10, w01, w11));
	return float2(max(max(w00, w10), max(w01, w11)), lerp(lerp(present.x, present.y, f.x), lerp(present.z, present.w, f.x), f.y));
}

float WaterNearTexels(int3 a_t)
{
	const uint sq = WaterNear.Load(a_t);
	return sq > 9u ? 3.0 : sqrt((float)sq);
}

float SampleWaterNear(float2 gridLocal)
{
	SHELL_TERRAIN_TEXEL(gridLocal);
	int r0 = (int)TerrainDim - 1 - t0.y;
	int r1 = (int)TerrainDim - 1 - t1.y;
	float n00 = WaterNearTexels(int3(t0.x, r0, 0));
	float n10 = WaterNearTexels(int3(t1.x, r0, 0));
	float n01 = WaterNearTexels(int3(t0.x, r1, 0));
	float n11 = WaterNearTexels(int3(t1.x, r1, 0));
	return lerp(lerp(n00, n10, f.x), lerp(n01, n11, f.x), f.y);
}

float TouchDownToe(float depth)
{
	return depth > 0.0 ? depth * smoothstep(3.0, 9.0, depth) : depth;
}

float KneeMin(float a, float b, float k)
{
	float e = a - b;
	float knee = e > k ? e : (e < -k ? 0.0 : (e + k) * (e + k) / (4.0 * k));
	return a - knee;
}

float TerrainHeightAt(float2 gridLocal)
{
	SHELL_TERRAIN_TEXEL(gridLocal);
	float s00 = TerrainWindow.Load(int3(t0.x, t0.y, 0)).x;
	float s10 = TerrainWindow.Load(int3(t1.x, t0.y, 0)).x;
	float s01 = TerrainWindow.Load(int3(t0.x, t1.y, 0)).x;
	float s11 = TerrainWindow.Load(int3(t1.x, t1.y, 0)).x;
	[flatten] if (min(min(s00, s10), min(s01, s11)) <= -50000.0)
		return -100000.0;
	return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}

static const float kLandFilm = 0.5;
static const float kLandFilmDepth = 1.0;
static const float kDecalShowDepth = 14.0;

float LandSnowWeight(float2 gridLocal)
{
	SHELL_TERRAIN_TEXEL(gridLocal);
	float b00 = TerrainBare.Load(int3(t0.x, t0.y, 0));
	float b10 = TerrainBare.Load(int3(t1.x, t0.y, 0));
	float b01 = TerrainBare.Load(int3(t0.x, t1.y, 0));
	float b11 = TerrainBare.Load(int3(t1.x, t1.y, 0));
	return 1.0 - lerp(lerp(b00, b10, f.x), lerp(b01, b11, f.x), f.y);
}

bool LandRectNoSnow(float2 lo, float2 hi)
{
	SHELL_TERRAIN_RANGE(lo, hi);
	float bare = 1.0;
	[loop] for (int y = i0.y; y <= i1.y; ++y)
	{
		[loop] for (int x = i0.x; x <= i1.x; ++x)
			bare = min(bare, TerrainBare.Load(int3(x, y, 0)));
	}
	return bare >= 0.998;
}

float LandSnowOdds(float masksY)
{
	return masksY > 0.002 && masksY < 1.5 ? saturate((masksY - 0.004) * (1.0 / 0.996)) * 32.0 - 16.0 : -16.0;
}

float LandSnowSide(float remaining, float carve)
{
	return CompactLook.y * remaining - CompactLook.z * smoothstep(0.3, 1.0, carve);
}

static const float kShoreRise = 10.0;

float3 EndSnowAtWater(float3 terrain, float2 gridLocal)
{
	[branch] if (terrain.x > -50000.0 && terrain.y > -7.9)
	{
		float2 waterAt = SampleWaterHeight(gridLocal);
		float water = waterAt.x;
		float waterWeight = waterAt.y;
		[branch] if (waterWeight > 0.001)
		{
			float h = TerrainHeightAt(gridLocal);
			[branch] if (h > -50000.0)
			{
				float rise = h - water + kWaterSkimDepth;
				float capRise = rise * (38.0 / kShoreRise);
				float capNear = (SampleWaterNear(gridLocal) - 1.0) * 38.0;
				float cap = max(capRise, capNear) + (1.0 - waterWeight) * 60.0;
				terrain.y = max(KneeMin(terrain.y, cap, 4.0), -8.0);
			}
		}
	}
	return terrain;
}

float ShellSurfaceZ(float2 gridLocal, out float coverage, out float terrainHeight)
{
	float edgeFade = ShellEdgeFade(gridLocal);
	float surfaceZ;
	[branch] if (edgeFade <= 0.0)
	{
		terrainHeight = SampleTerrain(gridLocal).x;
		coverage = 0.0;
		surfaceZ = terrainHeight - 32.0;
	}
	else
	{
		float3 terrain = SampleTerrain(gridLocal);
		terrain = EndSnowAtWater(terrain, gridLocal);

		terrainHeight = terrain.x;
		float rampDepth = terrain.y;
		coverage = saturate(terrain.z);

		float2 shelterMask = SampleExclusionMask(GridOrigin + gridLocal);

		coverage *= saturate(1.0 - shelterMask.x);

		float bare = saturate(1.0 - coverage);
		float depth = rampDepth + (-8.0) * bare;
		depth = ShellEdgeDepth(depth, edgeFade);

		depth = TouchDownToe(depth);
		depth = lerp(depth, min(depth, kFireMeltFloor), saturate(shelterMask.y));

		[flatten] if (depth > 0.0)
		{
			float deformation = saturate(SampleDeformationSmooth(gridLocal));
			float4 bermPair = BermFieldPair(gridLocal);
			float bermD = bermPair.x;
			float uncarved = depth;
			depth = CarveProfile(deformation, bermPair.y, uncarved, GridOrigin + gridLocal, TrackCarveLook(TrackAgeOf(bermPair.z)));
#ifndef SNOW_SURFACE_NO_BERM
			depth += BermShape(bermD) * saturate(1.0 - deformation) * uncarved * BermHeightAmp * BermDepthGate(uncarved) * TrackBermScale(TrackAgeOf(bermPair.w));
#endif
			depth += UndulationSampled(GridOrigin + gridLocal) * saturate(depth / 8.0);
		}

		surfaceZ = terrainHeight + depth;
	}
	return surfaceZ;
}

float2 ShellVertexSlope(float2 gridLocal)
{
	const float2 c = ShellSeamClamp(gridLocal);
	const float2 p = ShellSeamClamp(c + 32.0);
	const float2 n = ShellSeamClamp(c - 32.0);
	float hxp = SampleTerrain(float2(p.x, c.y)).x;
	float hxn = SampleTerrain(float2(n.x, c.y)).x;
	float hyp = SampleTerrain(float2(c.x, p.y)).x;
	float hyn = SampleTerrain(float2(c.x, n.y)).x;
	return float2(hxp - hxn, hyp - hyn) * (64.0 / max(p - n, 1.0));
}

#define SHELL_TERRAIN_NORMAL(slopeX, slopeY) normalize(float3(-(slopeX) / 64.0, -(slopeY) / 64.0, 1.0))

#define SHELL_DRAPE(position, footXY, z, terrainHeight, terrainNormal)                                      \
	[branch] if (SlopeDrape.x > 0.001 && (z) > (terrainHeight))                                             \
	{                                                                                                       \
		float drapeAbove = (z) - (terrainHeight);                                                           \
		float drapeTilt = SlopeDrape.x * (1.0 - smoothstep(SlopeDrape.y, SlopeDrape.z, (terrainNormal).z)); \
		float3 drapeDir = normalize(lerp(float3(0.0, 0.0, 1.0), (terrainNormal), drapeTilt));               \
		position = float3((footXY), (terrainHeight)) + drapeDir * drapeAbove;                               \
	}

#define SHELL_CAST_VIS(castVis, gridLocal, z, coverage)                                                                               \
	float3 rawTerrainCast = SampleTerrain(gridLocal);                                                                                 \
	float castVis = smoothstep(2.0, 5.0, (z) - rawTerrainCast.x) * smoothstep(0.2, 0.5, min((coverage), saturate(rawTerrainCast.z))); \
	castVis *= 1.0 - smoothstep(0.35, 0.6, SampleMelted(gridLocal));                                                                  \
	castVis *= 1.0 - smoothstep(2800.0, 4900.0, length((gridLocal) - WarpedHalfSpan));

#ifndef SNOW_SURFACE_ONLY
VS_OUTPUT FinishShellVertexSloped(float2 gridLocal, float z, float coverage, float terrainHeight, float slopeX, float slopeY)
{
	float3 terrainNormal = SHELL_TERRAIN_NORMAL(slopeX, slopeY);

	float3 absolutePos = float3(GridOrigin + gridLocal, z);
	SHELL_DRAPE(absolutePos, GridOrigin + gridLocal, z, terrainHeight, terrainNormal)

	float3 rel = absolutePos - ShellCameraPosAdjust.xyz;

	VS_OUTPUT vsout;
	vsout.Position = mul(CameraViewProj, float4(rel, 1.0));
	vsout.WorldPos = rel;
	vsout.GridLocal = gridLocal;
	vsout.CurrentClip = float4(0.0, 0.0, 0.0, mul(CameraViewProjUnjittered, float4(rel, 1.0)).w);
	vsout.PreviousClip = 0.0;
	vsout.TerrainNormalAlpha = float4(terrainNormal, 0.0);
	return vsout;
}

VS_OUTPUT FinishShellVertex(float2 gridLocal, float z, float coverage, float terrainHeight)
{
	const float2 slope = ShellVertexSlope(gridLocal);
	return FinishShellVertexSloped(gridLocal, z, coverage, terrainHeight, slope.x, slope.y);
}
#endif

#if defined(HULLSHADER) || defined(SNOW_SURFACE_ONLY)
static const float kTessNear = 1600.0;
static const float kTessMax = 8.0;
static const float kTessReachBoost = 3.0;
static const float kTessCutoff = kTessNear * kTessReachBoost;
#endif

#include "SnowDeformation/SnowParallax.hlsli"

#if defined(DOMAINSHADER) || defined(SNOW_SURFACE_ONLY)
float ShellVertexZFrom(float2 gridLocal, float z, float coverage, float terrainHeight)
{
	float camDist = length(GridOrigin + gridLocal - ShellCameraPosAdjust.xy);
	float2 snowUV = (SnowUVOffset + gridLocal) / kSnowUVTile;
	float snowMip = clamp(log2(max(camDist, 64.0) / 128.0), 0.0, 6.0);

	float descentBlend = SnowHeightBlendSharpness(camDist);
	float wEdge = smoothstep(0.0, 0.6, coverage);
	[branch] if (HasSnowHeight > 0.5 && descentBlend > 1.0 && z > terrainHeight && wEdge < 0.999)
	{
		float hDescent = SampleSnowHeight(ComputeSnowTapsNoGrad(snowUV, GridOrigin + gridLocal), 0.0.xx, snowMip);
		float billowW = (1.0 - smoothstep(0.05, 0.4, saturate(SampleDeformationSmooth(gridLocal)))) *
		                (1.0 - saturate(SampleExclusionMask(GridOrigin + gridLocal).y * 2.0));
		const float kEdgeBillowShift = 0.35;
		float wBillow = saturate(wEdge + (hDescent - 0.5) * kEdgeBillowShift * billowW);
		float descent = SnowHeightBlendOneSided(wBillow, hDescent, 1.0 + (descentBlend - 1.0) * 0.5);
		z = terrainHeight + (z - terrainHeight) * descent - 8.0 * (1.0 - descent);
	}

	[branch] if (HasSnowHeight > 0.5 && SnowReliefDepth > 0.01)
	{
		float reliefFade = 1.0 - smoothstep(600.0, 2200.0, camDist);
		float depthAbove = z - terrainHeight;
		[branch] if (reliefFade > 0.001 && depthAbove > 0.5)
		{
			SnowTaps reliefTaps = ComputeSnowTapsNoGrad(snowUV, GridOrigin + gridLocal);
			float h = SampleSnowHeight(reliefTaps, 0.0.xx, snowMip);
			float carve = saturate(SampleDeformationSmooth(gridLocal));
			z += (h - 0.5) * SnowReliefDepth * reliefFade * saturate(depthAbove / 6.0) * (1.0 - carve);
		}
	}

	return z;
}

float ShellVertexZ(float2 gridLocal, out float coverage, out float terrainHeight)
{
	float z = ShellSurfaceZ(gridLocal, coverage, terrainHeight);
	return ShellVertexZFrom(gridLocal, z, coverage, terrainHeight);
}
#endif

#ifdef PSHADER

struct PS_OUTPUT
{
	SNOW_GBUFFER_TARGETS
};

struct PS_PREPASS_OUTPUT
{
	float RasterDepth : SV_Target0;
#	ifdef SNOW_PREPASS_ATTR
	float4 AttrGrid : SV_Target1;
	float4 AttrGridDeriv : SV_Target2;
	float4 AttrDepth : SV_Target3;
#		ifdef SNOW_PREPASS_EARLY
	float4 Pending : SV_Target4;
#		endif
#	endif
	float DepthLE : SV_DepthLessEqual;
};

static const float kShellClampOccluderSlack = 8.0;
float ShellSceneAboveSnowLine(float3 worldPosRel, float sceneZ, float shellZ, float2 pixel)
{
	float3 sceneRel = worldPosRel * (sceneZ / max(shellZ, 1e-3));
	float2 sceneLocal = sceneRel.xy + ShellCameraPosAdjust.xy - GridOrigin;
	float2 tt = (GridToTerrainOffset + sceneLocal) / TerrainTexelSize;
	bool inWindow = all(tt >= 0.0) && all(tt <= (float)(TerrainDim - 1));
	float3 st = SampleTerrain(sceneLocal);
	float above = (sceneRel.z + ShellCameraPosAdjust.z) - (st.x + max(st.y, 0.0));
	above = (inWindow && st.x > -50000.0) ? above : 0.0;
#ifdef SNOW_PREPASS_EARLY
	return above;
#else
	bool sceneIsStatic = LandMasksCopy.Load(int3(pixel, 0)).y >= 1.5;
	return sceneIsStatic ? 1e6 : above;
#endif
}

float ShellSceneViewDepth(float a_rawDepth)
{
	return SharedData::GetScreenDepth(a_rawDepth / (TessScreen.w > 0.5 ? TessScreen.w : 1.0));
}

bool ShellFarClampWindow(float shellZ, float sceneZ)
{
	float clampWindow = min(8.0 + shellZ * 0.008, 48.0);
	return shellZ > 4000.0 && shellZ > sceneZ && shellZ - sceneZ < clampWindow;
}

bool ShellNearClampWindow(float shellZ, float sceneZ)
{
	return shellZ > sceneZ && shellZ - sceneZ < 0.75;
}

float ShellExportDepth(float rasterZ, float rawSceneDepth, float shellZ, float sceneZ, float pixelEffDepth, float pixelCarve, float sceneAbove)
{
	float depth = rasterZ;
	bool clampMode = sceneAbove < kShellClampOccluderSlack;
	[branch] if (clampMode && ShellFarClampWindow(shellZ, sceneZ))
		depth = min(rasterZ, rawSceneDepth - 1e-5);
	else if (clampMode && (pixelEffDepth < 4.0 || pixelCarve > 0.5) && ShellNearClampWindow(shellZ, sceneZ))
		depth = min(rasterZ, rawSceneDepth - 1e-5);
	return depth;
}

float ShellExportDepthAt(float rasterZ, float rawSceneDepth, float shellZ, float sceneZ, float pixelEffDepth, float pixelCarve, float3 worldPosRel, float2 pixel)
{
	float sceneAbove = 0.0;
	[branch] if (ShellFarClampWindow(shellZ, sceneZ) || ShellNearClampWindow(shellZ, sceneZ))
		sceneAbove = ShellSceneAboveSnowLine(worldPosRel, sceneZ, shellZ, pixel);
	return ShellExportDepth(rasterZ, rawSceneDepth, shellZ, sceneZ, pixelEffDepth, pixelCarve, sceneAbove);
}

bool ShellPrepassHidden(float rasterZ, float rawSceneDepth, float shellZ, float sceneZ)
{
	return !ShellFarClampWindow(shellZ, sceneZ) && !ShellNearClampWindow(shellZ, sceneZ) && rasterZ > rawSceneDepth + 1e-6;
}

bool ShellPrepassQuadHidden(float rasterZ, float rawSceneDepth, float shellZ, float sceneZ)
{
	float hidden = ShellPrepassHidden(rasterZ, rawSceneDepth, shellZ, sceneZ) ? 1.0 : 0.0;
	float hx = ddx_fine(hidden);
	return hidden > 0.5 && hx == 0.0 && ddy_fine(hidden) == 0.0 && ddy_fine(hx) == 0.0;
}

#ifdef SNOW_SHELL_DEPTH_PREPASS
Texture2D<float> TBMaskDepth : register(t110);
static const float kTBMaskReach = 1000.0;

bool ShellMasksNotLand(float3 worldPosRel, float2 pixel, float shellZ, float sceneZ)
{
	bool notLand = !ShellFarClampWindow(shellZ, sceneZ) && ShellNearClampWindow(shellZ, sceneZ);
	[branch] if (!notLand && dot(worldPosRel, worldPosRel) < kTBMaskReach * kTBMaskReach)
	{
		const float mask = TBMaskDepth.Load(int3(pixel, 0));
		notLand = mask > 0.0 && ShellSceneViewDepth(mask) - sceneZ < 0.05;
	}
	return notLand;
}
#endif

#if defined(SNOW_SHELL_DEFERRED)
void ShellClusterCoords(float3 relPos, out float viewZ, out float2 clusterUV)
{
	viewZ = mul(CameraView, float4(relPos, 1.0)).z;
	float4 clip = mul(CameraViewProj, float4(relPos, 1.0));
	clusterUV = clip.xy / max(clip.w, 1e-4) * float2(0.5, -0.5) + 0.5;
}

float3 ShellLuminanceBump(float3 normalWS, float2 worldXY, float bumpFade)
{
	const float kBumpTile = 64.0;
	const float kBumpHeight = 0.55;
	float2 texDims;
	SnowDiffuse.GetDimensions(texDims.x, texDims.y);
	float e = 1.5 / texDims.x;
	float2 detailUV = worldXY / kBumpTile;
	const float3 kLum = float3(0.30, 0.45, 0.25);
	const float2 detailDx = gDeferredGridDx / kBumpTile;
	const float2 detailDy = gDeferredGridDy / kBumpTile;
	float h0 = dot(SnowDiffuse.SampleGrad(SnowSampler, detailUV, detailDx, detailDy).rgb, kLum);
	float hx = dot(SnowDiffuse.SampleGrad(SnowSampler, detailUV + float2(e, 0.0), detailDx, detailDy).rgb, kLum);
	float hy = dot(SnowDiffuse.SampleGrad(SnowSampler, detailUV + float2(0.0, e), detailDx, detailDy).rgb, kLum);
	float2 bumpGrad = float2(hx - h0, hy - h0) * (kBumpHeight / (e * kBumpTile));
	return normalize(normalWS + float3(-bumpGrad * bumpFade, 0.0));
}
#endif

#if defined(SNOW_S1_RESOLVE)
Texture2D<float> EarlyRasterDepth : register(t61);
Texture2D<float2> EarlyPending : register(t62);

struct S1_RESOLVE_OUTPUT
{
	float Marks : SV_Target0;
	float Depth : SV_Depth;
};

S1_RESOLVE_OUTPUT main(float4 pos : SV_Position)
{
	const int3 px = int3(pos.xy, 0);
	const float raster = EarlyRasterDepth.Load(px);
	if (raster == 0.0)
		discard;
	float depth = abs(raster);
	[branch] if (raster < 0.0)
	{
		const float2 pending = EarlyPending.Load(px);
		const uint flags = (uint)(pending.y * 0.5);
		const float hLandRaw = LandMasksCopy.Load(px).y;
		[branch] if ((flags & 4) != 0)
		{
			if (LandSnowOdds(hLandRaw) + pending.x < 0.0)
				discard;
		}
		if ((flags & 1) != 0 && hLandRaw < 1.5)
			depth = min(depth, SceneDepth.Load(px) - 1e-5);
	}
	S1_RESOLVE_OUTPUT o;
	o.Marks = abs(raster);
	o.Depth = depth;
	return o;
}
#else

#if defined(SNOW_SHELL_DEFERRED)
PS_OUTPUT ShellShade(VS_OUTPUT input)
#else
PS_PREPASS_OUTPUT main(VS_OUTPUT input)
#endif
{
	float2 motionVector = float2(-0.5, 0.5) * (input.CurrentClip.xy / input.CurrentClip.w - input.PreviousClip.xy / input.PreviousClip.w);

	float2 gridLocal = input.GridLocal;
	float rawSceneDepth = SceneDepth.Load(int3(input.Position.xy, 0));
	float sceneZ = ShellSceneViewDepth(rawSceneDepth);
	float shellZ = input.CurrentClip.w;
#ifdef SNOW_PREPASS_ATTR
	const float4 attrGridDeriv = float4(ddx_coarse(input.GridLocal), ddy_coarse(input.GridLocal));
	const float2 attrZDeriv = float2(ddx_coarse(input.WorldPos.z), ddy_coarse(input.WorldPos.z));
#endif
#if defined(SNOW_SHELL_DEPTH_PREPASS) && !defined(SNOW_PREPASS_KEEP_HIDDEN)
	[branch] if (ShellPrepassQuadHidden(input.Position.z, rawSceneDepth, shellZ, sceneZ))
	{
		discard;
		return (PS_PREPASS_OUTPUT)0;
	}
#endif

#ifdef SNOW_SHELL_DEFERRED
	float pixelRampDepth = gDeferredRampDepth;
	float3 pixelTerrain = 0.0;
	float pixelCoverage = 1.0;
	float pixelClassDepth = 0.0;
#else
	float3 pixelTerrain = EndSnowAtWater(SampleTerrain(gridLocal), gridLocal);
	float pixelCoverage = saturate(pixelTerrain.z);
	float psEdgeFade = ShellEdgeFade(gridLocal);

	float pixelClassDepth = pixelTerrain.y;
	float pixelBare = saturate(1.0 - pixelCoverage);
	float pixelRampDepth = pixelClassDepth + (-8.0) * pixelBare;
	pixelRampDepth = ShellEdgeDepth(pixelRampDepth, psEdgeFade);
#endif

	float pixelEffDepth = TouchDownToe(pixelRampDepth);
#ifdef SNOW_SHELL_DEFERRED
	float coverageAlpha = 1.0;
#else
	const float pixelSnowWeight = LandSnowWeight(gridLocal);
	float coverageAlpha = pixelSnowWeight > 0.002 ? 1.0 : 0.0;
#endif
	float sheetAboveGround = smoothstep(0.25, 1.5, input.WorldPos.z + ShellCameraPosAdjust.z - pixelTerrain.x);
	coverageAlpha = max(coverageAlpha, sheetAboveGround);

	float objectFadeBand = 5.0 + shellZ * 0.004;
	float proximityFade = saturate((sceneZ - shellZ) / objectFadeBand);
	[branch] if (shellZ < 8192.0)
	{
		float sceneSurfaceZ = ShellCameraPosAdjust.z + input.WorldPos.z * (sceneZ / max(shellZ, 1e-3));
		float shellSurfaceZ = ShellCameraPosAdjust.z + input.WorldPos.z;
		float objectness = smoothstep(-1.0, 2.0, sceneSurfaceZ - shellSurfaceZ);
		proximityFade = max(proximityFade, 1.0 - objectness);
	}
	const bool pixelUntouched = ShellUntouched(gridLocal);
	float pixelCarve = 0.0;
	[branch] if (!pixelUntouched)
		pixelCarve = saturate(SampleDeformationSmooth(gridLocal));
	float2 pixelShelter = SampleExclusionMask(GridOrigin + gridLocal);
	float pixelMelt = saturate(pixelShelter.y);
	float carveOverride = smoothstep(0.1, 0.5, pixelCarve) * smoothstep(0.5, 1.0, pixelTerrain.y);
	bool landFilm = false;
	float landRemaining = 0.0;
	float landBurial = 0.0;
	float landMelt = 0.0;
#ifndef SNOW_SHELL_DEFERRED
	[branch] if (coverageAlpha > 0.001)
	{
		landRemaining = CarveProfile(pixelCarve, pixelCarve, max(pixelEffDepth, 0.0), GridOrigin + gridLocal, TrackCarveLook(0.0));
		landFilm = landRemaining < kLandFilmDepth;
		landMelt = min(SampleMelted(gridLocal), pixelCarve);
		landBurial = landMelt > 0.0 ? CarveProfile(landMelt, landMelt, max(pixelEffDepth, 0.0), GridOrigin + gridLocal, TrackCarveLook(0.0)) : max(pixelEffDepth, 0.0);
	}
#endif
	coverageAlpha *= landFilm ? 1.0 : max(proximityFade, saturate(carveOverride + smoothstep(0.1, 0.4, pixelMelt)));

#ifndef SNOW_SHELL_DEFERRED
#	ifdef SNOW_SHELL_DEPTH_PREPASS
	[branch] if (coverageAlpha > 0.001 && (landFilm || pixelSnowWeight < 0.998) && LandSnowSide(landRemaining, pixelCarve) < 16.0)
	{
		[flatten] if (ShellMasksNotLand(input.WorldPos, input.Position.xy, shellZ, sceneZ))
			coverageAlpha = 0.0;
	}
#	endif
#	ifdef SNOW_PREPASS_EARLY
	bool landContest = false;
	float landSide = 0.0;
	[branch] if (coverageAlpha > 0.001 && (landFilm || pixelSnowWeight < 0.998))
	{
		landSide = LandSnowSide(landBurial, landMelt);
		landContest = landSide < 16.0;
	}
#	else
	[branch] if (coverageAlpha > 0.001 && (landFilm || pixelSnowWeight < 0.998))
	{
		[flatten] if (LandSnowOdds(LandMasksCopy.Load(int3(input.Position.xy, 0)).y) + LandSnowSide(landBurial, landMelt) < 0.0)
			coverageAlpha = 0.0;
	}
#	endif
#endif
	float screenNoise = Random::InterleavedGradientNoise(input.Position.xy, SharedData::FrameCount);
#ifdef SNOW_SHELL_DEFERRED
	coverageAlpha = 1.0;
#else
	{
		float dust = saturate((coverageAlpha - 0.2) * (1.0 / 0.3));
		if (screenNoise * screenNoise >= dust)
			discard;
		coverageAlpha = 1.0;
	}
#endif

	float exportEffDepth = lerp(pixelEffDepth, min(pixelEffDepth, kFireMeltFloor), pixelMelt);
#ifdef SNOW_SHELL_DEPTH_PREPASS
	PS_PREPASS_OUTPUT prepassOut;
	prepassOut.RasterDepth = input.Position.z;
#	ifdef SNOW_PREPASS_ATTR
	prepassOut.AttrGrid = float4(input.GridLocal, normalize(input.TerrainNormalAlpha.xyz).xy);
	prepassOut.AttrGridDeriv = attrGridDeriv;
	prepassOut.AttrDepth = float4(input.CurrentClip.w, attrZDeriv, pixelRampDepth);
#	endif
	prepassOut.DepthLE = ShellExportDepthAt(input.Position.z, rawSceneDepth, shellZ, sceneZ, exportEffDepth, pixelCarve, input.WorldPos, input.Position.xy);
#	ifdef SNOW_PREPASS_EARLY
	const bool clampPending = prepassOut.DepthLE < input.Position.z;
	bool markPending = false;
	[branch] if (landRemaining < kDecalShowDepth)
		markPending = SampleSpellMark(gridLocal) > 0.0;
	prepassOut.Pending = 0.0;
	[branch] if (clampPending || landContest || markPending)
	{
		prepassOut.RasterDepth = -input.Position.z;
		prepassOut.Pending = float4(landSide, (clampPending ? 2.0 : 0.0) + (landContest ? 8.0 : 0.0), 0.0, 0.0);
	}
#	endif
	return prepassOut;
#endif
#ifndef SNOW_SHELL_DEPTH_PREPASS

	float3 terrainNormal = normalize(input.TerrainNormalAlpha.xyz);
	float pixelDepth = max(TouchDownToe(pixelRampDepth), 0.0);
	pixelDepth = lerp(pixelDepth, min(pixelDepth, kFireMeltFloor), pixelMelt);
	float2 profileGrad = 0.0;
	float2 bermGrad = 0.0;
	float bermCenter = 0.0;
	float pixelAge = 0.0;
	[branch] if (!pixelUntouched)
	{
		float bermAge = 0.0;
		[branch] if (TrackAge.y > 0.0)
		{
			const float2 dates = BermFieldPair(gridLocal).zw;
			pixelAge = TrackAgeOf(dates.x);
			bermAge = TrackAgeOf(dates.y);
		}
		const float step = 4.0;
		float dXP = SampleDeformationSmooth(gridLocal + float2(step, 0.0));
		float dXN = SampleDeformationSmooth(gridLocal - float2(step, 0.0));
		float dYP = SampleDeformationSmooth(gridLocal + float2(0.0, step));
		float dYN = SampleDeformationSmooth(gridLocal - float2(0.0, step));
		const float3 carveLook = TrackCarveLook(pixelAge);
		float4 blur = saturate(float4(dXP, dXN, dYP, dYN));
		[branch] if (CompactLook.w > 0.0 || carveLook.x > 0.0)
		{
			blur = float4(BermFieldPair(gridLocal + float2(step, 0.0)).y, BermFieldPair(gridLocal - float2(step, 0.0)).y,
				BermFieldPair(gridLocal + float2(0.0, step)).y, BermFieldPair(gridLocal - float2(0.0, step)).y);
		}
		profileGrad = float2(
						  CarveProfile(saturate(dXP), blur.x, pixelDepth, GridOrigin + gridLocal + float2(step, 0.0), carveLook) - CarveProfile(saturate(dXN), blur.y, pixelDepth, GridOrigin + gridLocal - float2(step, 0.0), carveLook),
						  CarveProfile(saturate(dYP), blur.z, pixelDepth, GridOrigin + gridLocal + float2(0.0, step), carveLook) - CarveProfile(saturate(dYN), blur.w, pixelDepth, GridOrigin + gridLocal - float2(0.0, step), carveLook)) /
		              (2.0 * step);
		const float bStep = BermGradStep();
		float mXP = dXP, mXN = dXN, mYP = dYP, mYN = dYN;
		[branch] if (bStep < step - 0.01)
		{
			mXP = SampleDeformationSmooth(gridLocal + float2(bStep, 0.0));
			mXN = SampleDeformationSmooth(gridLocal - float2(bStep, 0.0));
			mYP = SampleDeformationSmooth(gridLocal + float2(0.0, bStep));
			mYN = SampleDeformationSmooth(gridLocal - float2(0.0, bStep));
		}
		float bermXP = BermField(gridLocal + float2(bStep, 0.0));
		float bermXN = BermField(gridLocal - float2(bStep, 0.0));
		float bermYP = BermField(gridLocal + float2(0.0, bStep));
		float bermYN = BermField(gridLocal - float2(0.0, bStep));
		bermGrad = float2(
			BermShape(bermXP) * saturate(1.0 - mXP) - BermShape(bermXN) * saturate(1.0 - mXN),
			BermShape(bermYP) * saturate(1.0 - mYP) - BermShape(bermYN) * saturate(1.0 - mYN)) / (2.0 * bStep) * TrackBermScale(bermAge);
		bermCenter = 0.25 * (bermXP + bermXN + bermYP + bermYN);
	}
	float2 macroGrad = -terrainNormal.xy / max(terrainNormal.z, 0.1);
	float2 gradZ = macroGrad + profileGrad + bermGrad * pixelDepth * BermHeightAmp * BermDepthGate(pixelDepth);

	float2 worldXYPS = GridOrigin + gridLocal;
	float undScale = saturate(pixelDepth / 8.0);
	[branch] if (undScale > 0.001)
	{
		gradZ += UndulationGradSampled(worldXYPS) * undScale;
	}

	const float detailFade = 1.0 - smoothstep(1200.0, 2000.0, shellZ);
	float detailMask = 0.0;
	float detailRelief = 0.0;
	TrenchDetailTaps detailTaps = (TrenchDetailTaps)0;
	float detailLod = 0.0;
	float detailHitH = 0.0;
	float detailImpact = 0.0;
	const float2 detailPlane = gradZ;
	float3 shadePos = input.WorldPos;
#if defined(SNOW_NOISE_TABLE)
	[branch] if (!pixelUntouched && detailFade > 0.0 && (TrenchDetailRelief > 0.001 || abs(TrenchDetailRoughness - 1.0) > 0.001))
	{
		const float4 detailField = BermFieldPair(gridLocal);
		const float detailReach = detailFade;
		detailMask = TrenchDetailMask(pixelCarve, detailField.xy, TrenchDetailCoverage) * detailReach;
		[branch] if (detailMask > 0.0 || SpeedSpoil > 0.5)
		{
			const float2 marks = SampleDetailMarks(gridLocal);
			detailMask = max(detailMask, SpeedSpoil * TrenchSpoilMask(marks.y) * detailReach);
			[branch] if (detailMask > 0.0)
			{
				const float meltWet = max(smoothstep(kSpellMarkStart, 0.15, marks.x), smoothstep(0.1, 0.4, pixelMelt));
				const float detailAge = lerp(TrackAgeOf(detailField.w), TrackAgeOf(detailField.z), saturate(10.0 * pixelCarve));
				const float impact = marks.y * TrackDetailImpactScale(detailAge);
				detailImpact = max(meltWet, impact);
				detailRelief = TrenchDetailRelief * detailMask * max(saturate(pixelDepth / 10.0), meltWet) * TrampleDetailScale(worldXYPS, pixelCarve) *
				               (1.0 + kImpactReliefBoost * impact) * TrackDetailReliefScale(detailAge);
				detailTaps = TrenchDetailTapsAt(worldXYPS);
				detailLod = TrenchDetailLod(gDeferredGridDx, gDeferredGridDy);
			}
		}
	}
	[branch] if (detailRelief > 0.05)
	{
		float3 dir = normalize(input.WorldPos);
		const float4 duv = TrenchDetailStep(detailTaps, dir.xy);
		float t = TrenchDetailParallaxT(dir, detailPlane, detailTaps, duv,
			float3(detailRelief, detailLod, lerp(0.6, 2.0, saturate(shellZ / 1500.0))), detailImpact, screenNoise);
		detailHitH = (dir.z - dot(dir.xy, detailPlane)) * t;
		shadePos = input.WorldPos + dir * t;
		worldXYPS += dir.xy * t;
		gridLocal += dir.xy * t;
		detailTaps.uv += duv * t;
	}
	[branch] if (detailRelief > 0.001)
		gradZ += TrenchDetailGrad(detailTaps, detailLod, detailImpact) * detailRelief;
#endif

	float3 normalWS = normalize(float3(gradZ * -1.0, 1.0));
	const float3 frameNormalWS = normalize(float3(-detailPlane, 1.0));

	float bumpFade = 1.0 - smoothstep(600.0, 2200.0, shellZ);
	float scorchRaw = 0.0;
	float2 snowUV = (SnowUVOffset + gridLocal) / kSnowUVTile;
	SnowTaps snowTaps = ComputeSnowTapsNoGrad(snowUV, worldXYPS);
	snowTaps.duvdx = gDeferredGridDx / kSnowUVTile;
	snowTaps.duvdy = gDeferredGridDy / kSnowUVTile;
	float snowHeightMip = SnowHeightMipGrad(snowTaps.duvdx, snowTaps.duvdy);
	float snowSteepness = smoothstep(0.75, 0.55, abs(frameNormalWS.z));
	float snowWorldZAbs = shadePos.z + ShellCameraPosAdjust.z;
	bool snowSideDropsX = abs(frameNormalWS.x) > abs(frameNormalWS.y);
	float2 snowSidePlane = snowSideDropsX ? float2(worldXYPS.y, snowWorldZAbs) : float2(worldXYPS.x, snowWorldZAbs);
	float2 snowUVSide = (snowSidePlane - 4096.0 * floor(snowSidePlane / 4096.0)) / kSnowUVTile;
	SnowTaps snowTapsSide = ComputeSnowTapsNoGrad(snowUVSide, snowSidePlane);
	snowTapsSide.duvdx = (snowSideDropsX ? float2(gDeferredGridDx.y, gDeferredZDx) : float2(gDeferredGridDx.x, gDeferredZDx)) / kSnowUVTile;
	snowTapsSide.duvdy = (snowSideDropsX ? float2(gDeferredGridDy.y, gDeferredZDy) : float2(gDeferredGridDy.x, gDeferredZDy)) / kSnowUVTile;
	float snowHeightMipSide = SnowHeightMipGrad(snowTapsSide.duvdx, snowTapsSide.duvdy);
	SNOW_TANGENT_FRAME(bumpT, bumpB, frameNormalWS);

	float3 V = -normalize(input.WorldPos);

	const bool glintSide = snowSteepness > 0.5;
	const float2 glintDuvdx = glintSide ? snowTapsSide.duvdx : snowTaps.duvdx;
	const float2 glintDuvdy = glintSide ? snowTapsSide.duvdy : snowTaps.duvdy;

	const float iceParallax = SnowIceParallax(pixelCarve, pixelMelt, input.WorldPos.z + ShellCameraPosAdjust.z, gridLocal, true);
	[branch] if (HasSnowHeight > 0.5 && SnowParallax.z > 0.001 && bumpFade > 0.001)
	{
		float3x3 snowTbn = float3x3(bumpT, bumpB, frameNormalWS);
		DisplacementParams pomParams = SnowDisplacementParams();
		pomParams.HeightScale *= SnowParallax.z * iceParallax;
		float2 pomOffset = SnowParallaxOffset(snowTaps, snowUV, V, snowTbn, shellZ, snowHeightMip, screenNoise, pomParams);
		pomOffset *= 1.0 - smoothstep(0.15, 0.35, 1.0 - abs(frameNormalWS.z));
		snowUV += pomOffset;
		snowTaps = OffsetSnowTaps(snowTaps, pomOffset);
	}

	[branch] if (HasSnowNormal > 0.5 && bumpFade > 0.001)
		normalWS = normalize(normalWS + bumpFade * SampleSnowNormalOffset(SnowNormalMap, snowTaps, snowTapsSide, snowSteepness, normalWS,
										   bumpT, bumpB, snowSideDropsX));
	else if (HasSnowTexture != 0 && bumpFade > 0.001)
		normalWS = ShellLuminanceBump(normalWS, worldXYPS, bumpFade);

	float3 viewNormal = normalize(mul((float3x3)CameraView, normalWS));

	float3 kSnowAlbedo = float3(0.82, 0.84, 0.88);
	[branch] if (HasSnowTexture != 0)
	{
		kSnowAlbedo = SampleSnowPlanar(SnowDiffuse, snowTaps, snowTapsSide, snowSteepness).rgb;
		kSnowAlbedo = SnowColor::SetAlbedo(kSnowAlbedo, SnowTextureIsLinear != 0.0);
	}
	const float2 wetLook = SnowWetPrint(kSnowAlbedo, pixelCarve, pixelMelt, pixelDepth, input.WorldPos.z + ShellCameraPosAdjust.z, gridLocal, true);
	float kSnowRoughness = 0.6;
	float3 kSnowF0 = float3(0.028, 0.028, 0.028);

	float snowRoughness = kSnowRoughness;
	float3 snowF0 = kSnowF0;
	float snowAO = 1.0;
	SNOW_RMAOS(snowRoughness, snowF0, snowAO, snowTaps, snowTapsSide, snowSteepness)
	snowRoughness = clamp(snowRoughness * lerp(1.0, TrenchDetailRoughness, detailMask), 0.05, 1.0);

	snowRoughness *= 1.0 - wetLook.x;
	const float glintsLeft = wetLook.y;

	float3 L = SharedData::DirLightDirection.xyz;
	float satNdotL = saturate(dot(normalWS, L));
	float satNdotV = saturate(abs(dot(normalWS, V)) + 1e-5);

	float worldShadow = ShadowSampling::GetWorldShadow(shadePos, ShellCameraPosAdjust.xyz);
	float farShadowT = smoothstep(6000.0, 15000.0, length(input.WorldPos));
	float sunShadow;
	[branch] if (CrispShadows > 0.5)
	{
		sunShadow = worldShadow * SnowShadow::GetCascadeShadow(shadePos, normalWS, lerp(1.0, 6.0, farShadowT), uint2((uint)BorderStyle.z, (uint)BorderStyle.w));
	}
	else
	{
		float detailedShadow;
		float dynamicShadow = ShadowSampling::GetLightingShadow(shadePos, detailedShadow);
		sunShadow = worldShadow * min(dynamicShadow, detailedShadow);
	}

	float pixelVertexAO = -1.0;
	[branch] if ((ShellFlags.x & 1) != 0 && sunShadow > 0.01 && satNdotL > 0.001 && L.z > 0.01)
	{
		const float sunLen2D = max(length(L.xy), 1e-4);
		const float sunTan = L.z / sunLen2D;
		const float2 stepDir = L.xy / sunLen2D;
		const float surfZ = shadePos.z + ShellCameraPosAdjust.z;
		const bool nearCache = MarchCacheCovers(gridLocal, stepDir, kSunMarchDist[4]);
		[branch] if (nearCache || MarchFarCovers(gridLocal, stepDir, kSunMarchDist[4]))
		{
			const float soft = lerp(0.03, 0.35, farShadowT);
			float marchLight;
			[branch] if (nearCache)
			{
				const float h0 = MarchHeightNear(gridLocal);
				const float marchRef = max(h0 > -50000.0 ? h0 : surfZ, surfZ - 2.0);
				marchLight = MarchNearLight(MarchNearHorizon(gridLocal, stepDir, sunTan, marchRef), sunTan, soft, lerp(2.0, 8.0, farShadowT));
			}
			else
			{
				float h[6];
				MarchCachedHeights(gridLocal, stepDir, false, h);
				const float marchRef = max(h[0] > -50000.0 ? h[0] : surfZ, surfZ - 2.0);
				float horizonTan = -10.0;
				[unroll] for (uint marchI = 0; marchI < 5; marchI++)
					horizonTan = max(horizonTan, (h[marchI + 1] - marchRef - 1.0) / kSunMarchDist[marchI]);
				marchLight = smoothstep(-1.5 * soft, 1.5 * soft, sunTan - horizonTan);
			}
			const float lowSun = smoothstep(0.04, 0.12, sunTan);
			sunShadow *= lerp(1.0, lerp(marchLight, 1.0, 0.7 * farShadowT), lowSun);
		}
	}

	const bool sssSeesSnow = ScreenSpaceShadowsActive > 1.5;
	[branch] if (sssSeesSnow && !SharedData::InInterior && dot(normalWS, L) >= 0.0)
		sunShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.Position.xyz, float2(0.0, 0.0), 0.0);

	[branch] if (!sssSeesSnow && ScreenSpaceShadowsActive > 0.5 && SnowShadow::GetSssHandoff(shellZ) > 0.0)
	{
		float sssRayGap = sceneZ - shellZ;
		float sssVertGap = abs(input.WorldPos.z) * sssRayGap / max(shellZ, 1e-3);
		float sssBlend = (1.0 - smoothstep(8.0, 24.0, sssVertGap)) *
		                 (1.0 - smoothstep(150.0, 400.0, sssRayGap));
		[branch] if (ObjectLiftCap > 0.0 && sssBlend > 0.001)
		{
			float2 sunXY = L.xy / max(length(L.xy), 1e-4);
			float sssSurfZ = input.WorldPos.z + ShellCameraPosAdjust.z;
			[unroll] for (uint sssI = 0; sssI < 4; sssI++)
			{
				float2 tapWorldXY = GridOrigin + gridLocal + sunXY * (15.0 + 55.0 * sssI);
				float2 tapDims;
				bool tapValid;
				float2 tapTexel = ObjectMapTexel(tapWorldXY, tapDims, tapValid);
				[flatten] if (tapValid)
				{
					float tapTop = ObjectTopsRaw.Load(int3((int2)tapTexel, 0));
					[flatten] if (tapTop > -50000.0 && tapTop > sssSurfZ - 16.0)
						sssBlend = 0.0;
				}
			}
		}
		sssBlend *= SnowShadow::GetSssHandoff(shellZ);
		float sssMask = ScreenSpaceShadows::GetScreenSpaceShadow(input.Position.xyz, float2(0.0, 0.0), 0.0);
		sunShadow *= lerp(1.0, sssMask, sssBlend);
	}

	[branch] if (HasSnowHeight > 0.5 && SnowParallax.y > 0.001 && bumpFade > 0.001 &&
		sunShadow > 0.01 && satNdotL > 0.001)
	{
		float2 lightUV = float2(dot(L, bumpT), dot(L, bumpB));
		float2 lightUVSide = snowSideDropsX ? float2(L.y, L.z) : float2(L.x, L.z);
		float occlusion = SnowParallaxOcclusionPlanar(snowTaps, snowTapsSide, snowSteepness,
			lightUV, lightUVSide, snowHeightMip, snowHeightMipSide,
			SnowParallaxQuality(shellZ), screenNoise, SnowDisplacementParams());

		float parallaxShadow = 1.0 - saturate(occlusion * SnowParallax.y * iceParallax);
		sunShadow *= lerp(1.0, parallaxShadow, bumpFade * (1.0 - snowSteepness));
	}
#if defined(SNOW_NOISE_TABLE)
	[branch] if (detailRelief > 0.05 && TrenchDetailShadow > 0.001 && SnowParallax.y > 0.001 && sunShadow > 0.01 && satNdotL > 0.001)
		sunShadow *= lerp(1.0, TrenchDetailSunShadow(detailTaps, detailHitH, L, detailPlane, float3(detailRelief, detailLod, SnowParallax.y * iceParallax), detailImpact, screenNoise),
			min(TrenchDetailShadow, SnowParallax.z * iceParallax));
#endif

	float3 sunLight = SharedData::DirLightColor.xyz * sunShadow;

	const float2 glintUV = glintSide ? (snowSidePlane - 4096.0 * floor(snowSidePlane / 4096.0)) / kSnowUVTile :
	                                   fmod(GridOrigin + gridLocal, 4096.0) / kSnowUVTile;
	bool glintsLit = sunShadow != 0.0;
	[branch] if (!glintsLit && PointLightsActive > 0.5)
	{
		float glintViewZ;
		float2 glintClusterUV;
		ShellClusterCoords(input.WorldPos, glintViewZ, glintClusterUV);
		glintsLit = SnowLights::ClusterLightCount(glintClusterUV, glintViewZ) > 0;
	}
	const float glintsHere = glintsLit ? EnableGlints * glintsLeft : 0.0;
	SnowMaterialCtx snowMtl = SnowBuildMaterial(normalWS, kSnowAlbedo, snowRoughness, snowF0, snowAO,
		SnowGlintParams, glintsHere, glintUV, glintDuvdx, glintDuvdy, input.Position.xy);
	SnowSunLighting sunLit = SnowEvaluateSunPBR(snowMtl, normalWS, V, shadePos, ShellCameraPosAdjust.xyz, sunShadow,
		glintUV, glintDuvdx, glintDuvdy, float4(0.0, 0.0, 0.0, 0.0));
	float3 specularLobe = sunLit.specularLobe;
	float3 diffuseLobe = sunLit.diffuseLobe;
	float3 directDiffuse = sunLit.directDiffuse;
	float3 directSpecular = sunLit.directSpecular;
	float3 markSpecular = sunLit.markSpecular;

	[branch] if (PointLightsActive > 0.5)
	{
		float viewZ;
		float2 clusterUV;
		ShellClusterCoords(input.WorldPos, viewZ, clusterUV);
		SnowLights::AccumulatePointLights(snowMtl, shadePos, shadePos + ShellCameraPosAdjust.xyz,
			normalWS, V, viewZ, clusterUV, glintUV, glintDuvdx, glintDuvdy, float4(-1.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0),
			directDiffuse, directSpecular, markSpecular);
	}

	float3 ambientColor = SnowAmbientColor(normalWS);
	float3 ambientPart = ambientColor * diffuseLobe;
	[branch] if (pixelVertexAO < 0.0)
		pixelVertexAO = SampleTerrainVertexAO(gridLocal);
	float landVertexAO = ColorManagement::SRGBToWorking(pixelVertexAO);
	SNOW_SKYLIGHTING(ambientPart, diffuseLobe, directDiffuse, directSpecular, landVertexAO, shadePos, normalWS);
	float3 preLit = ambientPart + directDiffuse;

	float alpha = coverageAlpha;
	float stochasticBlend = (screenNoise * screenNoise) < alpha ? 1.0 : 0.0;

	PS_OUTPUT psout;
	psout.Diffuse = float4(preLit, alpha);
	psout.MotionVectors = float4(motionVector, 0.0, alpha);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(viewNormal), 1.0 - snowRoughness, stochasticBlend);
	psout.Albedo = float4(diffuseLobe, alpha);
	psout.Specular = float4(directSpecular, alpha);
	psout.Reflectance = float4(specularLobe, alpha);
	psout.Masks = float4(0.0, 0.0, Color::RGBToYCoCg(ambientPart).x, alpha);
	psout.Masks2 = float4(1.0 - landVertexAO, 0.0, 0.0, alpha);

	return psout;
#endif
}

#ifdef SNOW_SHELL_DEFERRED
Texture2D<float4> DeferredGrid : register(t91);
Texture2D<float4> DeferredGridDeriv : register(t92);
Texture2D<float4> DeferredDepth : register(t93);

PS_OUTPUT main(float4 pos : SV_Position)
{
	const int3 px = int3(pos.xy, 0);
	const float4 grid = DeferredGrid.Load(px);
	const float4 gridDeriv = DeferredGridDeriv.Load(px);
	const float4 depth = DeferredDepth.Load(px);
	gDeferredGridDx = gridDeriv.xy;
	gDeferredGridDy = gridDeriv.zw;
	gDeferredZDx = depth.y;
	gDeferredZDy = depth.z;
	gDeferredRampDepth = depth.w;

	const float w = depth.x;
	float3 rel;
	SHELL_DEFERRED_REL(rel, pos.xy, w)

	VS_OUTPUT input = (VS_OUTPUT)0;
	input.Position = float4(pos.xy, 0.0, 1.0);
	input.CurrentClip = mul(CameraViewProjUnjittered, float4(rel, 1.0));
	input.PreviousClip = mul(CameraPreviousViewProjUnjittered, float4(rel + ShellCameraPosAdjust.xyz - ShellCameraPreviousPosAdjust.xyz, 1.0));
	input.WorldPos = rel;
	input.GridLocal = grid.xy;
	input.TerrainNormalAlpha = float4(grid.zw, sqrt(saturate(1.0 - dot(grid.zw, grid.zw))), 1.0);
	return ShellShade(input);
}
#endif
#endif
#endif

#ifdef COMPUTESHADER
#ifdef SNOW_TERRAIN_CUT_CS
RWTexture2D<float4> TerrainWetOut : register(u4);

[numthreads(8, 8, 1)] void TerrainCutCS(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= TerrainDim))
		return;
	float4 texel = TerrainWindow.Load(int3(id.xy, 0));
	texel.xyz = WaterBareTexel(texel.xyz, int2(id.xy));
	TerrainWetOut[id.xy] = texel;
}
#endif

#if defined(SNOW_MARCH_HEIGHT_CS) || defined(SNOW_MARCH_SUN_CS)
#	include "SnowDeformation/SnowMarchCacheCB.hlsli"
#endif

#ifdef SNOW_MARCH_HEIGHT_CS
RWTexture2D<float> MarchHeightOut : register(u0);

float MarchTapHeight(float2 a_gridLocal)
{
	const float2 gridLocal = ShellSeamClamp(a_gridLocal);
	const float3 st = SampleTerrain(gridLocal);
	float depth = max(st.y, 0.0);
	depth = lerp(depth, min(depth, kFireMeltFloor), saturate(SampleExclusionMask(GridOrigin + gridLocal).y));
	const float deform = SampleDeformationFast(gridLocal);
	const float4 berm = BermBakeActive > 0.5 ? BermFieldBaked(gridLocal) : float4(0.0, deform, TrackAge.x, TrackAge.x);
	depth = CarveProfileNoTeeth(deform, berm.y, depth, TrackCarveLook(TrackAgeOf(berm.z))) +
	        BermShape(berm.x) * saturate(1.0 - deform) * depth * BermHeightAmp * BermDepthGate(depth) * TrackBermScale(TrackAgeOf(berm.w));
	return st.x + depth + Undulation(GridOrigin + gridLocal) * saturate(depth / 8.0);
}

void MarchHeightTexel(uint2 phys)
{
	SNOW_DEFORM_DIMS(dims);
	if (any(float2(phys) >= dims))
		return;
	const float2 logical = float2((int2(phys) - DeformMapOrigin) & (int2(dims) - 1));
	MarchHeightOut[phys] = MarchTapHeight((logical + 0.5) / dims / DeformInvWorldSize - GridToDeformOffset);
}

[numthreads(8, 8, 1)] void MarchHeightTilesCS(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
	const uint listIndex = Gid.y * 1024u + Gid.x;
	if (listIndex >= MarchTilesIn[0])
		return;
	const uint packed = MarchTilesIn[1 + listIndex];
	MarchHeightTexel(uint2(packed & 0xFFFFu, packed >> 16u) * 8u + GTid.xy);
}

[numthreads(8, 8, 1)] void MarchHeightRowsCS(uint3 DTid : SV_DispatchThreadID)
{
	SNOW_DEFORM_DIMS(dims);
	if (DTid.y >= MarchRowCount)
		return;
	MarchHeightTexel(uint2(DTid.x, (MarchRowStart + DTid.y) % (uint)dims.y));
}

[numthreads(8, 8, 1)] void MarchHeightFarRowsCS(uint3 DTid : SV_DispatchThreadID)
{
	uint2 dims;
	MarchHeightOut.GetDimensions(dims.x, dims.y);
	if (DTid.x >= dims.x || DTid.y >= MarchRowCount)
		return;
	const uint2 t = uint2(DTid.x, (MarchRowStart + DTid.y) % dims.y);
	MarchHeightOut[t] = MarchTapHeight(float2(t) * FineWindow.w - FineWindow.xy);
}
#endif

#ifdef SNOW_MARCH_SUN_CS
RWTexture2D<float> MarchSunOut : register(u0);

void MarchSunTexel(int2 a_phys)
{
	SNOW_DEFORM_DIMS(dims);
	const int2 phys = a_phys & (int2(dims) - 1);
	const float2 logical = float2((phys - DeformMapOrigin) & (int2(dims) - 1));
	const float texel = 1.0 / (DeformInvWorldSize * dims.x);
	const float2 gridLocal = (logical + 0.5) * texel - GridToDeformOffset;
	const float2 margin = 1.5 / dims;
	float envelope = -1e30;
	bool inside = true;
	[loop] for (uint k = 1; k <= kSunEnvelopeSteps && inside; k++)
	{
		const float s = SunEnvelopeRun(k) * texel;
		const float2 p = gridLocal + MarchSun.xy * s;
		const float2 uv = (GridToDeformOffset + p) * DeformInvWorldSize;
		inside = all(uv >= margin) && all(uv <= 1.0 - margin);
		[branch] if (inside)
		{
			float2 f;
			envelope = max(envelope, MarchCacheBilinear(MarchCacheTexels(MarchHeightGround, p, f), f) - s * MarchSun.z);
		}
	}
	MarchSunOut[phys] = envelope;
}

bool SunRunMeetsBox(float2 a_p, float2 a_v, float2 a_lo, float2 a_hi)
{
	float t0 = 0.0;
	float t1 = 1.0;
	bool meets = true;
	[unroll] for (uint i = 0; i < 2; i++)
	{
		[flatten] if (abs(a_v[i]) < 1e-6)
		{
			meets = meets && a_p[i] >= a_lo[i] && a_p[i] <= a_hi[i];
		}
		else
		{
			const float u0 = (a_lo[i] - a_p[i]) / a_v[i];
			const float u1 = (a_hi[i] - a_p[i]) / a_v[i];
			t0 = max(t0, min(u0, u1));
			t1 = min(t1, max(u0, u1));
		}
	}
	return meets && t0 <= t1;
}

[numthreads(8, 8, 1)] void MarchSunTilesCS(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
	const uint listIndex = Gid.y * 1024u + Gid.x;
	if (listIndex >= MarchTilesIn[0])
		return;
	const uint packed = MarchTilesIn[1 + listIndex];
	const int2 tile = int2(packed & 0xFFFFu, packed >> 16u) * 8;
	const float2 run = MarchSun.xy * (SunEnvelopeRun(kSunEnvelopeSteps) + 1.0);
	const int2 lo = tile + int2(floor(min(-run, 0.0))) - 1;
	const int2 hi = tile + 8 + int2(ceil(max(-run, 0.0))) + 1;
	const float2 boxLo = float2(tile) - 1.0;
	const float2 boxHi = float2(tile) + 9.0;
	[loop] for (int y = lo.y + (int)GTid.y; y < hi.y; y += 8)
	{
		[loop] for (int x = lo.x + (int)GTid.x; x < hi.x; x += 8)
		{
			[branch] if (SunRunMeetsBox(float2(x, y) + 0.5, run, boxLo, boxHi))
				MarchSunTexel(int2(x, y));
		}
	}
}

[numthreads(8, 8, 1)] void MarchSunRowsCS(uint3 DTid : SV_DispatchThreadID)
{
	SNOW_DEFORM_DIMS(dims);
	if (DTid.x >= (uint)dims.x || DTid.y >= MarchRowCount)
		return;
	MarchSunTexel(int2(DTid.x, (MarchRowStart + DTid.y) % (uint)dims.y));
}
#endif
#endif
