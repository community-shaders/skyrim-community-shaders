#include "SnowDeformation/SnowCommon.hlsli"

RWTexture2D<float4> TerrainWindow : register(u0);
Texture2D<float> HeightMap : register(t0);
Texture2D<float4> LODTile00 : register(t1);
Texture2D<float4> LODTile10 : register(t2);
Texture2D<float4> LODTile01 : register(t3);
Texture2D<float4> LODTile11 : register(t4);
SamplerState LinearSampler : register(s0);

cbuffer WindowFillCB : register(b0)
{
	float2 WindowOriginWorld;
	float TexelSize;
	uint WindowDim;

	float2 HeightMapScale;
	float2 HeightMapOffset;

	float2 HeightRange;
	float SnowDepthUnits;
	float padFill;

	float2 LODTileBase;
	float LODTileSpan;
	float LODSnowSensitivity;

	float4 LODTileValid;
}

float ClassifyLODSnow(float3 color)
{
	SNOW_TEXEL_SCORE(score, color, 0.62 - 0.64 * saturate(LODSnowSensitivity), 0.12);
	return score;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (id.x >= WindowDim || id.y >= WindowDim)
		return;
	if (TerrainWindow[id.xy].x > -50000.0)
		return;

	float2 worldXY = WindowOriginWorld + float2(id.xy) * TexelSize;
	float2 uv = worldXY * HeightMapScale + HeightMapOffset;
	if (any(uv < 0.0) || any(uv > 1.0))
		return;

	float z = lerp(HeightRange.x, HeightRange.y, HeightMap.SampleLevel(LinearSampler, uv, 0));

	float2 tileLocal = (worldXY - LODTileBase) / LODTileSpan;
	int2 tileIdx = clamp((int2)floor(tileLocal), 0, 1);
	uint tileFlat = uint(tileIdx.y) * 2u + uint(tileIdx.x);
	float coverage;
	float provenance;
	[branch] if (LODTileValid[tileFlat] > 0.5)
	{
		float2 tileUV = saturate(tileLocal - (float2)tileIdx);
		tileUV.y = 1.0 - tileUV.y;
		float3 lodColor;
		[branch] if (tileFlat == 0u)
			lodColor = LODTile00.SampleLevel(LinearSampler, tileUV, 0).rgb;
		else if (tileFlat == 1u)
			lodColor = LODTile10.SampleLevel(LinearSampler, tileUV, 0).rgb;
		else if (tileFlat == 2u)
			lodColor = LODTile01.SampleLevel(LinearSampler, tileUV, 0).rgb;
		else lodColor = LODTile11.SampleLevel(LinearSampler, tileUV, 0).rgb;
		coverage = ClassifyLODSnow(lodColor);
		provenance = 2.0 + coverage;
	}
	else
	{
		coverage = 0.0;
		provenance = 1.0;
	}

	TerrainWindow[id.xy] = float4(z, coverage * SnowDepthUnits, coverage, provenance);
}
