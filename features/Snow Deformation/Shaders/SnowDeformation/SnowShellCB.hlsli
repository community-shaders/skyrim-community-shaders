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
	uint LayoutKeeper6;
	uint TerrainDim;
	uint LayoutKeeper15;

	float DeformInvWorldSize;
	uint HasSnowTexture;
	float SnowTextureIsLinear;
	float HasSnowNormal;

	float HasSnowRmaos;
	float SnowRoughnessScale;
	float2 SnowUVOffset;

	float4 SnowGlintParams;

	float SnowSpecularLevel;
	float EnableGlints;
	float TrenchDetailRelief;
	float TrenchDetailRoughness;

	float LayoutKeeper5;
	float TrenchDetailCoverage;
	float ShellCullBare;
	float LayoutKeeper11;

	float LayoutKeeper12;
	float ObjectLiftCap;
	float2 ObjectHeightCenter;

	float ObjectHeightHalfExtent;
	float CrispShadows;
	float ScreenSpaceShadowsActive;
	float UndulationAmp;

	float UndulationScale;
	float LayoutKeeper13;
	float PointLightsActive;
	float SkylightingActive;

	float HasSnowHeight;
	float SnowReliefDepth;
	float StaticsDebugView;
	float BermHeightAmp;

	float SpeedSpoil;
	float LayoutKeeper9;
	float DebugNoFarPad;
	float LayoutKeeper4;

	float LayoutKeeper7;
	float TrenchDetailShadow;
	float LayoutKeeper3;
	float LayoutKeeper14;

	float BermReach;
	uint LayoutKeeper10;
	float SeamRampInv;
	float BermBakeActive;

	float4 SeamBounds;

	float4 ExclusionFieldWindow;

	float4 SnowParallax;

	float4 padSpellShading;
	float4 TrackAge;
	float4 padCrustLook2;
	float4 BorderStyle;
	float4 CompactLook;
	float4 RimStyle;

	float4 UndulationFieldWindow;

	int2 DeformMapOrigin;
	int2 ShellFlags;

	float4 FineWindow;
	float4 SlopeDrape;
	float4 padDebugSkinDepth;
	float4 BloodLook;
	float4 BloodLook2;
	float4 BloodLook3;
	float4 TrampleStyle;
	float4 TessScreen;
	float4 FieldDims;
	float4 SnowClimate;
	float4 SnowClimate2;
	float4 SnowClimateOverride;
	float4 SnowClimateLUT[3];
}

int3 DeformTexel(int2 t, int2 dims)
{
	return int3((t + DeformMapOrigin) & (dims - 1), 0);
}

#define SHELL_TERRAIN_TEXEL(gridLocal) TERRAIN_WINDOW_TEXEL((GridToTerrainOffset + (gridLocal)) / TerrainTexelSize, TerrainDim)

#endif
