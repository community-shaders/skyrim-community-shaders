#ifndef __SHARED_DATA_DEPENDENCY_HLSL__
#define __SHARED_DATA_DEPENDENCY_HLSL__

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"

namespace SharedData
{

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	cbuffer SharedData : register(b5)
	{
		float4 WaterData[25];
		row_major float3x4 DirectionalAmbient;
		float4 DirLightDirection;
		float4 DirLightColor;
		float4 CameraData;
		float4 BufferDim;
		float Timer;
		uint FrameCount;
		uint FrameCountAlwaysActive;
		bool InInterior;  // If the area lacks a directional shadow light e.g. the sun or moon
		bool InMapMenu;   // If the world/local map is open (note that the renderer is still deferred here)
		bool HideSky;     // HideSky flag in WorldSpace, e.g. Blackreach
		float MipBias;    // Offset to mip level for TAA sharpness#
	};

	struct GrassLightingSettings
	{
		float Glossiness;
		float SpecularStrength;
		float SubsurfaceScatteringAmount;
		bool OverrideComplexGrassSettings;

		float BasicGrassBrightness;
		float3 pad0;
	};

	struct CPMSettings
	{
		bool EnableComplexMaterial;
		bool EnableParallax;
		bool EnableTerrainParallax;
		bool EnableHeightBlending;
		bool EnableShadows;
		bool ExtendShadows;
		bool EnableParallaxWarpingFix;
		float1 pad0;
	};

	struct CubemapCreatorSettings
	{
		uint Enabled;
		float3 pad0;

		float4 CubemapColor;
	};

	struct TerraOccSettings
	{
		bool EnableTerrainShadow;
		float3 Scale;
		float2 ZRange;
		float2 Offset;
	};

	struct LightLimitFixSettings
	{
		uint EnableLightsVisualisation;
		uint LightsVisualisationMode;
		float2 pad0;
		uint4 ClusterSize;
	};

	struct WetnessEffectsSettings
	{
		row_major float4x4 OcclusionViewProj;

		float Time;
		float Raining;
		float Wetness;
		float PuddleWetness;

		bool EnableWetnessEffects;
		float MaxRainWetness;
		float MaxPuddleWetness;
		float MaxShoreWetness;

		uint ShoreRange;
		float PuddleRadius;
		float PuddleMaxAngle;
		float PuddleMinWetness;

		float MinRainWetness;
		float SkinWetness;
		float WeatherTransitionSpeed;
		bool EnableRaindropFx;

		bool EnableSplashes;
		bool EnableRipples;
        uint EnableVanillaRipples;
        float RaindropFxRange;

		float RaindropGridSizeRcp;
		float RaindropIntervalRcp;
		float RaindropChance;
		float SplashesLifetime;

		float SplashesStrength;
		float SplashesMinRadius;
		float SplashesMaxRadius;
		float RippleStrength;

		float RippleRadius;
		float RippleBreadth;
		float RippleLifetimeRcp;
		float pad0;
	};

	struct SkylightingSettings
	{
		row_major float4x4 OcclusionViewProj;
		float4 OcclusionDir;

		float4 PosOffset;   // xyz: cell origin in camera model space
		uint4 ArrayOrigin;  // xyz: array origin
		int4 ValidMargin;

		float MinDiffuseVisibility;
		float MinSpecularVisibility;
		uint2 pad0;
	};

	struct CloudShadowsSettings
	{
		float Opacity;
		float3 pad0;
	};

	struct LODBlendingSettings
	{
		float LODTerrainBrightness;
		float LODObjectBrightness;
		float LODObjectSnowBrightness;
		bool DisableTerrainVertexColors;
		float LODTerrainGamma;
		float LODObjectGamma;
		float LODObjectSnowGamma;
		float pad0;
	};

	struct HairSpecularSettings
	{
		uint Enabled;
		float HairGlossiness;
		float SpecularMult;
		float DiffuseMult;
		uint EnableTangentShift;
		float PrimaryTangentShift;
		float SecondaryTangentShift;
		float HairSaturation;
		float SpecularIndirectMult;
		float DiffuseIndirectMult;
		float BaseColorMult;
		float Transmission;
		uint EnableSelfShadow;
		float SelfShadowStrength;
		float SelfShadowExponent;
		float SelfShadowScale;
		uint HairMode;  // 0: Kajiya-Kay, 1: Marschner
		uint3 pad;
	};

	struct TerrainVariationSettings
	{
		uint enableTilingFix;
		uint enableLODTerrainTilingFix;
		float2 pad0;
	};

	struct IBLSettings
	{
		uint EnableDiffuseIBL;
		uint PreserveFogLuminance;
		uint UseStaticIBL;
		uint EnableInterior;
		float DiffuseIBLScale;
		float DALCAmount;
		float IBLSaturation;
		float FogAmount;
	};

	struct ExtendedTranslucencySettings
	{
		uint MaterialModel;  // [0,1,2,3] The MaterialModel
		float Reduction;     // [0, 1.0] The factor to reduce the transparency to matain the average transparency [0,1]
		float Softness;      // [0, 2.0] The soft remap upper limit [0,2]
		float Strength;      // [0, 1.0] The inverse blend weight of the effect
	};

	struct LinearLightingSettings
	{
		uint enableLinearLighting;
		uint enableGammaCorrection;
		uint isDirLightLinear;
		float dirLightMult;
		float lightGamma;
		float colorGamma;
		float emitColorGamma;
		float glowmapGamma;
		float ambientGamma;
		float fogGamma;
		float fogAlphaGamma;
		float effectGamma;
		float effectAlphaGamma;
		float skyGamma;
		float waterGamma;
		float vlGamma;
		float vanillaDiffuseMult;
		float vanillaSpecularMult;
		float grassDiffuseMult;
		float grassSpecularMult;
		float vanillaDiffuseColorMult;
		float lightMult;
		float directionalLightMult;
		float pointLightMult;
		float emitColorMult;
		float glowmapMult;
		float effectLightingMult;
		float membraneEffectMult;
		float bloodEffectMult;
		float projectedEffectMult;
		float deferredEffectMult;
		float otherEffectMult;
	};

	cbuffer FeatureData : register(b6)
	{
		GrassLightingSettings grassLightingSettings;
		CPMSettings extendedMaterialSettings;
		CubemapCreatorSettings cubemapCreatorSettings;
		TerraOccSettings terraOccSettings;
		LightLimitFixSettings lightLimitFixSettings;
		WetnessEffectsSettings wetnessEffectsSettings;
		SkylightingSettings skylightingSettings;
		CloudShadowsSettings cloudShadowsSettings;
		LODBlendingSettings lodBlendingSettings;
		HairSpecularSettings hairSpecularSettings;
		TerrainVariationSettings terrainVariationSettings;
		IBLSettings iblSettings;
		ExtendedTranslucencySettings extendedTranslucencySettings;
		LinearLightingSettings linearLightingSettings;
	};

	Texture2D<float4> DepthTexture : register(t17);

	// Get a int3 to be used as texture sample coord. [0,1] in uv space
	int3 ConvertUVToSampleCoord(float2 uv, uint a_eyeIndex)
	{
		uv = Stereo::ConvertToStereoUV(uv, a_eyeIndex);
		uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
		return int3(uv * BufferDim.xy, 0);
	}

	// Get a raw depth from the depth buffer. [0,1] in uv space
	float GetDepth(float2 uv, uint a_eyeIndex = 0)
	{
		return DepthTexture.Load(ConvertUVToSampleCoord(uv, a_eyeIndex)).x;
	}

	float GetScreenDepth(float depth)
	{
		return (CameraData.w / (-depth * CameraData.z + CameraData.x));
	}

	float4 GetScreenDepths(float4 depths)
	{
		return (CameraData.w / (-depths * CameraData.z + CameraData.x));
	}

	float GetScreenDepth(float2 uv, uint a_eyeIndex = 0)
	{
		float depth = GetDepth(uv, a_eyeIndex);
		return GetScreenDepth(depth);
	}

	float4 GetWaterData(float3 worldPosition)
	{
		float2 cellF = (((worldPosition.xy + FrameBuffer::CameraPosAdjust[0].xy)) / 4096.0) + 64.0;  // always positive
		int2 cellInt;
		float2 cellFrac = modf(cellF, cellInt);

		cellF = worldPosition.xy / float2(4096.0, 4096.0);  // remap to cell scale
		cellF += 2.5;                                       // 5x5 cell grid
		cellF -= cellFrac;                                  // align to cell borders
		cellInt = round(cellF);

		uint waterTile = (uint)clamp(cellInt.x + (cellInt.y * 5), 0, 24);  // remap xy to 0-24

		float4 waterData = float4(1.0, 1.0, 1.0, -2147483648);

		[flatten] if (cellInt.x < 5 && cellInt.x >= 0 && cellInt.y < 5 && cellInt.y >= 0)
			waterData = WaterData[waterTile];
		return waterData;
	}

#endif  // PSHADER
}
#endif  // __SHARED_DATA_DEPENDENCY_HLSL__