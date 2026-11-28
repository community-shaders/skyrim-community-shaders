#ifndef __SNOW_SHELL_CB_HLSLI__
#define __SNOW_SHELL_CB_HLSLI__

#include "SnowDeformation/SnowCommon.hlsli"

#ifndef SNOW_SHELL_CB_REGISTER
#	define SNOW_SHELL_CB_REGISTER b0
#endif
cbuffer ShellCB : register(SNOW_SHELL_CB_REGISTER)
{
	row_major float4x4 CameraViewProj;
	row_major float4x4 CameraViewProjUnjittered;
	row_major float4x4 CameraPreviousViewProjUnjittered;
	row_major float4x4 CameraView;

	float4 ShellCameraPosAdjust;
	float4 ShellCameraPreviousPosAdjust;

	float2 GridOrigin;
	float GridSpacing;
	float TerrainTexelSize;

	float2 GridToTerrainOffset;
	float2 GridToDeformOffset;

	float WarpedHalfSpan;
	uint TerrainDim;
	float DeformInvWorldSize;
	float ShellCullBare;

	uint HasSnowTexture;
	float SnowTextureIsLinear;
	float HasSnowNormal;
	float HasSnowRmaos;

	float SnowRoughnessScale;
	float SnowSpecularLevel;
	float2 SnowUVOffset;

	float4 SnowGlintParams;

	float EnableGlints;
	float HasSnowHeight;
	float TrenchDetailRelief;
	float TrenchDetailShadow;

	float2 ObjectHeightCenter;
	float ObjectHeightHalfExtent;
	float BermBakeActive;

	float CrispShadows;
	float ScreenSpaceShadowsActive;
	float PointLightsActive;
	float SkylightingActive;

	float2 SunCascadeSlices;
	float2 LandContest;

	float3 SnowParallax;
	float SeamRampInv;

	float4 SeamBounds;

	float4 ExclusionFieldWindow;

	float4 UndulationFieldWindow;

	int2 DeformMapOrigin;
	int2 ShellFlags;

	float4 FineWindow;
	float4 TessScreen;
	float4 FieldDims;

	float AccumulationDepthScale;
	float BodyPrint;
	float2 pad0;
}

// The trench look's fixed values.
static const float kTrenchDetailCoverage = 1.75;
static const float kBermHeight = 0.5;
static const float kRimLip = 0.05;
static const float kRimTeeth = 0.5;
static const float kTrampledFlattening = 0.5;

int3 DeformTexel(int2 t, int2 dims)
{
	return int3((t + DeformMapOrigin) & (dims - 1), 0);
}

#define SHELL_TERRAIN_TEXEL(gridLocal) TERRAIN_WINDOW_TEXEL((GridToTerrainOffset + (gridLocal)) / TerrainTexelSize, TerrainDim)

#endif
