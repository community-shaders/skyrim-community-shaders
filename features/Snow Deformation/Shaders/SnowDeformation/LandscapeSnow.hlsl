#ifndef VSHADER
#	define VSHADER
#endif

#define main LS_GameVS
#include "Utility.hlsl"
#undef main

#if defined(SKINNED) || defined(EYE)
#	error "landscape draws are never skinned"
#endif

#define SNOW_SURFACE_ONLY
#define SNOW_SHELL_CB_REGISTER b13
#include "SnowDeformation/SnowShell.hlsl"

#include "SnowDeformation/LandscapeSnowTess.hlsli"

#define LS_POS(v) (v).PositionMS

#if defined(RENDER_SHADOWMAP)
#	define LS_CASTER
static const float kLsCasterPush = 0.0008;
#else
#	error "landscape snow raises the shadow-map casters only"
#endif

float3 LsRaise(float3 p, out bool a_raised)
{
	float2 gridLocal = p.xy + LsGeometry.xy;
	float landZ = p.z + LsGeometry.z;
	float coverage;
	float terrainHeight;
	float z = ShellSurfaceZ(gridLocal, coverage, terrainHeight);
	SHELL_CAST_VIS(castVis, gridLocal, z, coverage)
	a_raised = castVis >= 0.35 && z > landZ;
	[branch] if (a_raised)
	{
		float3 raised = float3(0.0, 0.0, z);
		[branch] if (SlopeDrape.x > 0.001 && z > terrainHeight)
		{
			float2 slope = ShellVertexSlope(gridLocal);
			float3 terrainNormal = SHELL_TERRAIN_NORMAL(slope.x, slope.y);
			SHELL_DRAPE(raised, float2(0.0, 0.0), z, terrainHeight, terrainNormal)
		}
		p += float3(raised.xy, raised.z - landZ);
	}
	return p;
}

VS_OUTPUT LsGameVS(VS_INPUT a_input, bool a_raised)
{
	VS_OUTPUT o = LS_GameVS(a_input);
#if defined(LS_CASTER) && !defined(RENDER_SHADOWMAP_PB)
	[flatten] if (a_raised)
		o.PositionCS.z += kLsCasterPush * o.PositionCS.w;
#endif
	return o;
}

VS_OUTPUT LS_VSRaise(VS_INPUT input)
{
	bool raised;
	LS_POS(input).xyz = LsRaise(LS_POS(input).xyz, raised);
	return LsGameVS(input, raised);
}

struct LS_CP
{
	float4 PositionMS: LSPOS;
#if defined(TEXTURE)
	float2 TexCoord: LSUV;
#endif
#if defined(NORMALS)
	float4 Normal: LSNRM;
	float4 Bitangent: LSBIN;
#endif
#if defined(VC)
	float4 Color: LSCOL;
#endif
};

LS_CP LS_VS(VS_INPUT input)
{
	LS_CP o;
	o.PositionMS = input.PositionMS;
#if defined(TEXTURE)
	o.TexCoord = input.TexCoord;
#endif
#if defined(NORMALS)
	o.Normal = input.Normal;
	o.Bitangent = input.Bitangent;
#endif
#if defined(VC)
	o.Color = input.Color;
#endif
	return o;
}

float4 LsClip(float3 p)
{
	return mul(mul(FrameBuffer::CameraViewProj, World), float4(p, 1.0));
}

bool LsOffView(float3 p0, float3 p1, float3 p2)
{
	const float3 lift = float3(0.0, 0.0, LsGeometry.w);
	const float k = 1.0 + LsCull.x;
	float3 pts[6] = { p0, p1, p2, p0 + lift, p1 + lift, p2 + lift };
	LS_OFF_VIEW(k, LsClip(pts[i]))
#if defined(RENDER_SHADOWMAP)
	n = false;
#endif
	return l || r || b || t || n;
}

LS_HSConst LS_PatchConst(InputPatch<LS_CP, 3> p)
{
	LS_HSConst o;
	bool off = false;
	[branch] if (LsTess.w > 0.5)
		off = LsOffView(LS_POS(p[0]).xyz, LS_POS(p[1]).xyz, LS_POS(p[2]).xyz);
	o.Edges[0] = 0.0;
	o.Edges[1] = 0.0;
	o.Edges[2] = 0.0;
	o.Inside = 0.0;
	[branch] if (!off)
	{
		float3 g0 = float3(LS_POS(p[0]).xy + LsGeometry.xy, LS_POS(p[0]).z + LsGeometry.z);
		float3 g1 = float3(LS_POS(p[1]).xy + LsGeometry.xy, LS_POS(p[1]).z + LsGeometry.z);
		float3 g2 = float3(LS_POS(p[2]).xy + LsGeometry.xy, LS_POS(p[2]).z + LsGeometry.z);
		o.Edges[0] = LsEdgeFactor(g1, g2);
		o.Edges[1] = LsEdgeFactor(g2, g0);
		o.Edges[2] = LsEdgeFactor(g0, g1);
		o.Inside = LsInsideFactor(g0, g1, g2, max(o.Edges[0], max(o.Edges[1], o.Edges[2])));
	}
	return o;
}

[domain("tri")]
	[partitioning("fractional_odd")]
	[outputtopology("triangle_cw")]
	[outputcontrolpoints(3)]
	[patchconstantfunc("LS_PatchConst")]
	[maxtessfactor(64.0)] LS_CP
	LS_HS(InputPatch<LS_CP, 3> p, uint i : SV_OutputControlPointID) {
		return p[i];
	}

#define LS_LERP(f) (p[0].f * uvw.x + p[1].f * uvw.y + p[2].f * uvw.z)

		[domain("tri")] VS_OUTPUT LS_DS(LS_HSConst c, float3 uvw : SV_DomainLocation, const OutputPatch<LS_CP, 3> p)
{
	VS_INPUT vin;
	vin.PositionMS = LS_LERP(PositionMS);
#if defined(TEXTURE)
	vin.TexCoord = LS_LERP(TexCoord);
#endif
#if defined(NORMALS)
	vin.Normal = LS_LERP(Normal);
	vin.Bitangent = LS_LERP(Bitangent);
#endif
#if defined(VC)
	vin.Color = LS_LERP(Color);
#endif
	bool raised;
	LS_POS(vin).xyz = LsRaise(LS_POS(vin).xyz, raised);
	return LsGameVS(vin, raised);
}
