#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Color.hlsli"

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

namespace ShadowSampling
{

	Texture2DArray<float4> SharedShadowMap : register(t18);

	struct ShadowData
	{
		float4 VPOSOffset;
		float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
		float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
		float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
		float4 FocusShadowFadeParam;
		float4 DebugColor;
		float4 PropertyColor;
		float4 AlphaTestRef;
		float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
		float4x3 FocusShadowMapProj[4];
		// Since ShadowData is passed between c++ and hlsl, can't have different defines due to strong typing
		float4x3 ShadowMapProj[2][3];
		float4x4 CameraViewProjInverse[2];
	};

	StructuredBuffer<ShadowData> SharedShadowData : register(t19);

	float GetWorldShadow(float3 positionWS, float3 offset, uint eyeIndex)
	{
		if (SharedData::InInterior || SharedData::HideSky)
			return 1.0;

		float worldShadow = 1.0;
#if defined(TERRAIN_SHADOWS)
		worldShadow = TerrainShadows::GetTerrainShadow(positionWS + offset, LinearSampler);
#endif

#if defined(CLOUD_SHADOWS)
		if (!SharedData::InMapMenu)
			worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif

		return worldShadow;
	}

	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor, float skylightingDiffuse = 1.0)
	{
		float3 ambientColorAmb = max(0, mul(SharedData::DirectionalAmbient, float4(0, 0, 1, 1)));

	#   if defined(IBL)
		if (SharedData::iblSettings.EnableDiffuseIBL && (!SharedData::InInterior || SharedData::iblSettings.EnableInterior)) {
			ambientColorAmb *= SharedData::iblSettings.DALCAmount;
	#       if defined(SKYLIGHTING) && !defined(INTERIOR)
			float3 iblColor = Color::Saturation(ImageBasedLighting::GetIBLColor(float3(0, 0, -1), skylightingDiffuse), SharedData::iblSettings.IBLSaturation) * SharedData::iblSettings.DiffuseIBLScale;
	#       else
			float3 iblColor = Color::Saturation(ImageBasedLighting::GetIBLColor(float3(0, 0, -1)), SharedData::iblSettings.IBLSaturation) * SharedData::iblSettings.DiffuseIBLScale;
	#       endif
			ambientColorAmb += Color::IrradianceToGamma(iblColor);
		}
	#   endif

		float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) 
			? SharedData::linearLightingSettings.dirLightMult : 1.0f;
		float3 dirLightColorDir = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), 
			SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult * Color::EffectLightingMult();

		// Calculate total expected lighting and find scale to match input
		float3 totalLight = ambientColorAmb + dirLightColorDir;
		float3 scale = totalLight > 0.0 ? inputColor / totalLight : 1.0;
		
		// Distribute proportionally
		ambientColor = ambientColorAmb * scale;
		dirColor = dirLightColorDir * scale;
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__