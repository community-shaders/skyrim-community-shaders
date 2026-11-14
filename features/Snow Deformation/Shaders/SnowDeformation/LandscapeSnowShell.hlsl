#include "SnowDeformation/SnowShell.hlsl"

#include "SnowDeformation/LandscapeSnowTess.hlsli"

struct LSS_CP
{
	float3 Local : LSSLOCAL;
};

#if defined(VSHADER)
LSS_CP main(float4 position : POSITION)
{
	LSS_CP o;
	o.Local = float3(position.xy + LsGeometry.xy, position.z + LsGeometry.z);
	return o;
}
#endif

#if defined(HULLSHADER)
bool LssBeyondSeam(float2 lo, float2 hi)
{
	float2 liveLo = 0.0;
	float2 liveHi = 2.0 * WarpedHalfSpan;
	[flatten] if (SeamRampInv > 0.0)
	{
		liveLo = max(liveLo, SeamBounds.xy - GridOrigin);
		liveHi = min(liveHi, SeamBounds.zw - GridOrigin);
	}
	return any(hi <= liveLo) || any(lo >= liveHi);
}

bool LssFullyBare(float2 lo, float2 hi)
{
	return ShellTerrainAllBare(lo, hi) && LandRectNoSnow(lo, hi);
}

bool LssOffView(float3 g0, float3 g1, float3 g2)
{
	const float3 camLocal = float3(GridOrigin, 0.0) - ShellCameraPosAdjust.xyz;
	const float3 down = float3(0.0, 0.0, -32.0);
	const float3 up = float3(0.0, 0.0, LsGeometry.w);
	const float k = 1.0 + LsCull.x;
	float3 pts[6] = { g0 + down, g1 + down, g2 + down, g0 + up, g1 + up, g2 + up };
	LS_OFF_VIEW(k, mul(CameraViewProj, float4(pts[i] + camLocal, 1.0)))
	return l || r || b || t || n;
}

LS_HSConst LSS_PatchConst(InputPatch<LSS_CP, 3> p)
{
	LS_HSConst o;
	o.Edges[0] = 0.0;
	o.Edges[1] = 0.0;
	o.Edges[2] = 0.0;
	o.Inside = 0.0;
	const float3 g0 = p[0].Local;
	const float3 g1 = p[1].Local;
	const float3 g2 = p[2].Local;
	const float2 lo = min(g0.xy, min(g1.xy, g2.xy));
	const float2 hi = max(g0.xy, max(g1.xy, g2.xy));
	bool culled = LssBeyondSeam(lo, hi);
	[branch] if (!culled && LsTess.w > 0.5)
		culled = LssOffView(g0, g1, g2);
	[branch] if (!culled && ShellCullBare > 0.5 && all(hi - lo <= 2.0 * TerrainTexelSize))
		culled = LssFullyBare(lo, hi);
	[branch] if (!culled)
	{
		o.Edges[0] = 1.0;
		o.Edges[1] = 1.0;
		o.Edges[2] = 1.0;
		[branch] if (LsTess.y > 1.0)
		{
			o.Edges[0] = LsEdgeFactor(g1, g2);
			o.Edges[1] = LsEdgeFactor(g2, g0);
			o.Edges[2] = LsEdgeFactor(g0, g1);
		}
		o.Inside = LsInsideFactor(g0, g1, g2, max(o.Edges[0], max(o.Edges[1], o.Edges[2])));
	}
	return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("LSS_PatchConst")]
[maxtessfactor(64.0)]
LSS_CP main(InputPatch<LSS_CP, 3> p, uint i : SV_OutputControlPointID)
{
	return p[i];
}
#endif

#if defined(DOMAINSHADER)
[domain("tri")]
VS_OUTPUT main(LS_HSConst c, float3 uvw : SV_DomainLocation, const OutputPatch<LSS_CP, 3> p)
{
	float2 gridLocal = p[0].Local.xy * uvw.x + p[1].Local.xy * uvw.y + p[2].Local.xy * uvw.z;
	float coverage;
	float terrainHeight;
	float z = ShellVertexZ(gridLocal, coverage, terrainHeight);
	[branch] if (LandSnowWeight(gridLocal) > 0.002)
		z = max(z, p[0].Local.z * uvw.x + p[1].Local.z * uvw.y + p[2].Local.z * uvw.z + kLandFilm);
	return FinishShellVertex(gridLocal, z, coverage, terrainHeight);
}
#endif
