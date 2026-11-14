cbuffer LandscapeSnowCB : register(b11)
{
	float4 LsGeometry;
	float4 LsTess;
	float4 LsCull;
	float4 LsExcl[16];
	float4 LsExclAxis[16];
};

struct LS_HSConst
{
	float Edges[3] : SV_TessFactor;
	float Inside: SV_InsideTessFactor;
};

#define LS_OFF_VIEW(k, clip)                               \
	bool l = true, r = true, b = true, t = true, n = true; \
	[unroll] for (uint i = 0; i < 6; i++)                  \
	{                                                      \
		float4 c = clip;                                   \
		l = l && (c.x + c.w * k < 0.0);                    \
		r = r && (c.w * k - c.x < 0.0);                    \
		b = b && (c.y + c.w * k < 0.0);                    \
		t = t && (c.w * k - c.y < 0.0);                    \
		n = n && (c.z < 0.0);                              \
	}

#if defined(HULLSHADER) || defined(SNOW_SURFACE_ONLY)
bool LsBoxUntouched(float2 lo, float2 hi)
{
	float2 uvLo = (GridToDeformOffset + lo) * DeformInvWorldSize;
	float2 uvHi = (GridToDeformOffset + hi) * DeformInvWorldSize;
	float2 pad = 8.0 * DeformInvWorldSize;
	bool farOutside = any(uvHi < -pad) || any(uvLo > 1.0 + pad);
	bool inside = all(uvLo >= 0.0) && all(uvHi <= 1.0);
	bool untouched = farOutside;
	[branch] if (!farOutside && inside)
	{
		SNOW_DEFORM_DIMS(dims);
		int2 i0 = clamp(int2(uvLo * dims), 0, int2(dims) - 1);
		int2 i1 = clamp(int2(uvHi * dims), 0, int2(dims) - 1);
		untouched = all(i1 - i0 <= 16);
		[unroll] for (int y = 0; y < 3; y++)
		{
			[unroll] for (int x = 0; x < 3; x++)
			{
				int2 t = min(i0 + int2(x, y) * 8, i1);
				untouched = untouched && UntouchedTiles.Load(int3(((t + DeformMapOrigin) & (int2(dims) - 1)) >> 3, 0)) != 0;
			}
		}
	}
	return BermBakeActive > 0.5 && untouched;
}

static const float kLsBorderTolerance = 1.0;

void LsPlainTap(int2 t, inout float2 lowest, inout float2 highest)
{
	float2 depthCoverage = TerrainWindowWet.Load(int3(t, 0)).yz;
	depthCoverage.x = max(depthCoverage.x, 0.0) * RimStyle.w + min(depthCoverage.x, 0.0);
	lowest = min(lowest, depthCoverage);
	highest = max(highest, depthCoverage);
}

bool LsBoxPlain(float2 lo, float2 hi)
{
	float maxT = (float)(TerrainDim - 1);
	int2 t0 = (int2)clamp(floor((GridToTerrainOffset + lo) / TerrainTexelSize), 0.0, maxT);
	int2 t1 = (int2)clamp(floor((GridToTerrainOffset + hi) / TerrainTexelSize) + 1.0, 0.0, maxT);
	bool plain = all(t1 - t0 <= 5);
	float2 lowest = 1e9;
	float2 highest = -1e9;
	[branch] if (all(t1 - t0 <= 2))
	{
		[unroll] for (int y = 0; y < 3; y++)
		{
			[unroll] for (int x = 0; x < 3; x++)
				LsPlainTap(min(t0 + int2(x, y), t1), lowest, highest);
		}
	}
	else
	{
		[unroll] for (int y = 0; y < 6; y++)
		{
			[unroll] for (int x = 0; x < 6; x++)
				LsPlainTap(min(t0 + int2(x, y), t1), lowest, highest);
		}
	}
	float spread = (highest.x - lowest.x) + 8.0 * (saturate(highest.y) - saturate(lowest.y));
	return plain && spread < kLsBorderTolerance;
}

bool LsDryTap(int2 t0, int2 t1, int x, int y)
{
	int2 t = min(t0 + int2(x, y), t1);
	return WaterRawWindow.Load(int3(t.x, (int)TerrainDim - 1 - t.y, 0)) < -50000.0;
}

bool LsBoxDry(float2 lo, float2 hi)
{
	float maxT = (float)(TerrainDim - 1) - 0.001;
	int2 t0 = (int2)clamp((GridToTerrainOffset + lo) / TerrainTexelSize, 0.0, maxT);
	int2 t1 = min((int2)clamp((GridToTerrainOffset + hi) / TerrainTexelSize, 0.0, maxT) + 1, int2(TerrainDim - 1, TerrainDim - 1));
	bool dry = all(t1 - t0 <= 3);
	[branch] if (all(t1 - t0 <= 2))
	{
		[unroll] for (int y = 0; y < 3; y++)
		{
			[unroll] for (int x = 0; x < 3; x++)
				dry = dry && LsDryTap(t0, t1, x, y);
		}
	}
	else
	{
		[unroll] for (int y = 0; y < 4; y++)
		{
			[unroll] for (int x = 0; x < 4; x++)
				dry = dry && LsDryTap(t0, t1, x, y);
		}
	}
	return dry;
}

bool LsPointPlain(float2 gridLocal)
{
	float2 mask = SampleExclusionMask(GridOrigin + gridLocal);
	return max(mask.x, mask.y) < 0.001;
}

bool LsEdgeDetail(float2 a, float2 b, uint gate)
{
	float2 lo = min(a, b);
	float2 hi = max(a, b);
	bool detail = false;
	[branch] if ((gate & 1) != 0)
		detail = !LsBoxUntouched(lo, hi);
	[branch] if (!detail && (gate & 2) != 0)
		detail = !LsBoxPlain(lo, hi);
	[branch] if (!detail && (gate & 4) != 0)
		detail = !LsBoxDry(lo, hi);
	[branch] if (!detail && (gate & 16) != 0)
		detail = !(LsPointPlain(a) && LsPointPlain(b) && LsPointPlain(0.5 * (a + b)));
	return detail;
}

bool LsSegmentMeetsExcl(float2 a, float2 b, uint i)
{
	[flatten] if (a.x > b.x || (a.x == b.x && a.y > b.y))
	{
		float2 t = a;
		a = b;
		b = t;
	}
	float2 ax = LsExclAxis[i].xy;
	float2 ay = float2(-ax.y, ax.x);
	float2 h = LsExcl[i].zw + 4.0;
	float2 pa = float2(dot(a - LsExcl[i].xy, ax), dot(a - LsExcl[i].xy, ay));
	float2 d = float2(dot(b - a, ax), dot(b - a, ay));
	float t0 = 0.0;
	float t1 = 1.0;
	bool meets = true;
	[unroll] for (uint k = 0; k < 2; k++)
	{
		[flatten] if (abs(d[k]) < 1e-6)
		{
			meets = meets && abs(pa[k]) <= h[k];
		}
		else
		{
			float u0 = (-h[k] - pa[k]) / d[k];
			float u1 = (h[k] - pa[k]) / d[k];
			t0 = max(t0, min(u0, u1));
			t1 = min(t1, max(u0, u1));
		}
	}
	return meets && t0 <= t1;
}

bool LsEdgeMeetsExcl(float2 a, float2 b)
{
	bool meets = false;
	[loop] for (uint i = 0; i < (uint)LsCull.z && !meets; i++)
		meets = LsSegmentMeetsExcl(a, b, i);
	return meets;
}

bool LsTriangleHoldsExcl(float2 p0, float2 p1, float2 p2)
{
	bool holds = false;
	[loop] for (uint i = 0; i < (uint)LsCull.z && !holds; i++)
	{
		float2 c = LsExcl[i].xy;
		float d0 = (p1.x - p0.x) * (c.y - p0.y) - (p1.y - p0.y) * (c.x - p0.x);
		float d1 = (p2.x - p1.x) * (c.y - p1.y) - (p2.y - p1.y) * (c.x - p1.x);
		float d2 = (p0.x - p2.x) * (c.y - p2.y) - (p0.y - p2.y) * (c.x - p2.x);
		holds = (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) || (d0 <= 0.0 && d1 <= 0.0 && d2 <= 0.0);
	}
	return holds;
}

float LsDetailFactor(float3 a, float3 b)
{
	float2 mid = 0.5 * (a.xy + b.xy);
	float dist = length(mid - (ShellCameraPosAdjust.xy - GridOrigin));
	float spacing = ShellBandStep(mid);
	[branch] if (LsTess.x > 0.5)
	{
		float reliefBase = SnowReliefDepth > 0.01 ? 1.0 : 0.0;
		float reach = kTessNear * reliefBase;
		[branch] if (dist < kTessCutoff)
		{
			float deform = max(max(SampleDeformationFast(a.xy), SampleDeformationFast(b.xy)), SampleDeformationFast(mid));
			reach = kTessNear * lerp(reliefBase, kTessReachBoost, smoothstep(0.02, 0.25, deform));
		}
		spacing /= clamp(reach / max(dist, 32.0), 1.0, kTessMax);
	}
	float factor = clamp(distance(a.xy, b.xy) / spacing, 1.0, LsTess.y);
	bool wall = false;
	[branch] if (LsTess.x > 0.5)
	{
		float lo = 1.0;
		float hi = 0.0;
		[unroll] for (uint i = 0; i < 5; i++)
		{
			const float d = saturate(SampleDeformationFast(lerp(a.xy, b.xy, i * 0.25)));
			lo = min(lo, d);
			hi = max(hi, d);
		}
		wall = hi - lo > 0.15;
		[branch] if (wall)
		{
			SNOW_DEFORM_DIMS(dims);
			factor = max(factor, min(distance(a.xy, b.xy) * DeformInvWorldSize * dims.x / 1.5, 64.0));
		}
	}
	[branch] if (LsTess.x > 0.5 && TessScreen.x > 0.0 && factor > 1.0)
	{
		float3 camLocal = float3(GridOrigin, 0.0) - ShellCameraPosAdjust.xyz;
		float4 clipA = mul(CameraViewProj, float4(a + camLocal, 1.0));
		float4 clipB = mul(CameraViewProj, float4(b + camLocal, 1.0));
		[flatten] if (min(clipA.w, clipB.w) > 16.0)
			factor = min(factor, max(length((clipA.xy / clipA.w - clipB.xy / clipB.w) * TessScreen.yz) / ((wall ? 0.25 : 1.0) * TessScreen.x), 1.0));
	}
	return factor;
}

float LsEdgeFactor(float3 a, float3 b)
{
	float2 mid = 0.5 * (a.xy + b.xy);
	float factor = 1.0;
	bool live = ShellEdgeFade(a.xy) > 0.0 || ShellEdgeFade(b.xy) > 0.0 || ShellEdgeFade(mid) > 0.0;
	[branch] if (live && LsTess.z > 0.5)
		live = LsEdgeDetail(a.xy, b.xy, (uint)LsTess.z) || (((uint)LsTess.z & 16) != 0 && LsEdgeMeetsExcl(a.xy, b.xy));
	[branch] if (live)
		factor = LsDetailFactor(a, b);
	return factor;
}

float LsInsideFactor(float3 g0, float3 g1, float3 g2, float a_edges)
{
	float inside = a_edges;
	[branch] if (LsCull.z > 0.5 && ((uint)LsTess.z & 16) != 0 && LsTriangleHoldsExcl(g0.xy, g1.xy, g2.xy))
		inside = max(inside, max(LsDetailFactor(g1, g2), max(LsDetailFactor(g2, g0), LsDetailFactor(g0, g1))));
	return inside;
}
#endif
