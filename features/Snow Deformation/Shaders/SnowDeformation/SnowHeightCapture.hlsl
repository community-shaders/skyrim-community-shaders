#include "SnowDeformation/SnowCommon.hlsli"

cbuffer StaticCB : register(b1)
{
#include "SnowDeformation/StaticCBFields.hlsli"
}

#if defined(WATER)
struct WATER_VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float WorldZ : TEXCOORD0;
};

#	if defined(VSHADER)
WATER_VS_OUTPUT main(float4 position : POSITION0)
{
	float3 posMS = position.xyz;
	float3 worldAbs = float3(
		dot(WorldRow0.xyz, posMS) + WorldRow0.w,
		dot(WorldRow1.xyz, posMS) + WorldRow1.w,
		dot(WorldRow2.xyz, posMS) + WorldRow2.w);
	float2 ndc = (worldAbs.xy - HeightWindowCenter) / HeightHalfExtent;
	WATER_VS_OUTPUT vsout;
	vsout.Position = float4(ndc.x, ndc.y, 0.5, 1.0);
	vsout.WorldZ = worldAbs.z;
	return vsout;
}
#	endif

#	if defined(PSHADER)
float main(WATER_VS_OUTPUT input) : SV_Target0
{
	return input.WorldZ;
}
#	endif
#else

struct VS_INPUT
{
	float4 Position : POSITION0;
	float4 Normal : NORMAL0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float WorldZ : TEXCOORD0;
	float2 WorldXY : TEXCOORD1;
};

#ifdef VSHADER
VS_OUTPUT main(VS_INPUT input)
{
	float3 posMS = input.Position.xyz;
	float3 worldAbs = float3(
		dot(WorldRow0.xyz, posMS) + WorldRow0.w,
		dot(WorldRow1.xyz, posMS) + WorldRow1.w,
		dot(WorldRow2.xyz, posMS) + WorldRow2.w);

	float2 ndc = (worldAbs.xy - HeightWindowCenter) / HeightHalfExtent;

	VS_OUTPUT vsout;
	vsout.Position = float4(ndc.x, ndc.y, 0.5, 1.0);
	vsout.WorldZ = worldAbs.z;
	vsout.WorldXY = worldAbs.xy;
	return vsout;
}
#endif

#if defined(PSHADER)
cbuffer HeightProcessCB : register(b0)
{
	int2 ScrollDeltaH;
	uint ClearAllH;
	uint padConeStepH;
	float2 HeightWindowCenterH;
	float HeightHalfExtentH;
	float padSlopePerUnitH;
	float2 TerrainWindowOriginH;
	float TerrainTexelSizeH;
	uint TerrainDimH;
}

Texture2D<float4> TerrainWindowCapture : register(t2);

float2 main(VS_OUTPUT input) : SV_Target0
{
	float terrain = TerrainWindowHeight(TerrainWindowCapture, (input.WorldXY - TerrainWindowOriginH) / TerrainTexelSizeH, TerrainDimH);
	float bottom = input.WorldZ - terrain < 40.0 ? 100000.0 : input.WorldZ;
	return float2(input.WorldZ, -bottom);
}
#endif
#endif
