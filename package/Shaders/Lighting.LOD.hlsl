#define LIGHTING

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/LodLandscape.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Skinned.hlsli"
#include "Common/Triplanar.hlsli"
#include "Common/VR.hlsli"


#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif


#if defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(LODLANDNOISE) || defined(WORLD_MAP)
#	define LOD
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord0: TEXCOORD0;
#if !defined(MODELSPACENORMALS)
	float4 Normal: NORMAL0;
	float4 Bitangent: BINORMAL0;
#endif  // !MODELSPACENORMALS

#if defined(VC)
	float4 Color: COLOR0;
#endif      // VC
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
#if (defined(PROJECTED_UV)) || 0
	float4
#else
	float2
#endif  // (defined (PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
		TexCoord0: TEXCOORD0;

#if defined(WORLD_MAP)
	float3 InputPosition: TEXCOORD4;
#endif

#if 0 || !defined(MODELSPACENORMALS)
	float3 TBN0: TEXCOORD1;
	float3 TBN1: TEXCOORD2;
	float3 TBN2: TEXCOORD3;
#endif  // defined(SKINNED) || !defined(MODELSPACENORMALS)
#if defined(PROJECTED_UV)
	float3 TexProj: TEXCOORD7;
#endif  // EYE

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float4 Color: COLOR0;
	float4 FogParam: COLOR1;


	float3 ModelPosition: TEXCOORD12;
};
#ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
	float4 HighDetailRange[1] : packoffset(c0);  // loaded cells center in xy, size in zw
	float4 FogParam : packoffset(c1);
	float4 FogNearColor : packoffset(c2);
	float4 FogFarColor : packoffset(c3);
};

cbuffer PerMaterial : register(b1)
{
	float4 LeftEyeCenter : packoffset(c0);
	float4 RightEyeCenter : packoffset(c1);
	float4 TexcoordOffset : packoffset(c2);
};

cbuffer PerGeometry : register(b2)
{
	row_major float3x4 World[1] : packoffset(c0);
	row_major float3x4 PreviousWorld[1] : packoffset(c3);
	float4 EyePosition[1] : packoffset(c6);
	float4 LandBlendParams : packoffset(c7);  // offset in xy, gridPosition in yw
	float4 TreeParams : packoffset(c8);       // wind magnitude in y, amplitude in z, leaf frequency in w
	float2 WindTimers : packoffset(c9);
	row_major float3x4 TextureProj[1] : packoffset(c10);
	float IndexScale : packoffset(c13);
	float4 WorldMapOverlayParameters : packoffset(c14);
};

cbuffer VS_PerFrame : register(b12)
{
	row_major float3x3 ScreenProj[1] : packoffset(c0);
	row_major float4x4 ViewProj[1] : packoffset(c8);
};


VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	precise float4 inputPosition = float4(input.Position.xyz, 1.0);

	uint eyeIndex = Stereo::GetEyeIndexVS(
	);
#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	inputPosition = LodLandscape::AdjustLodLandscapeVertexPositionMS(inputPosition, float4x4(World[eyeIndex], float4(0, 0, 0, 1)), HighDetailRange[eyeIndex]);
#	endif  // defined(LODLANDNOISE) || defined(LODLANDSCAPE)                                                                   \

	precise float4 previousInputPosition = inputPosition;


	precise float4 previousWorldPosition = float4(mul(PreviousWorld[eyeIndex], inputPosition), 1);
	precise float4 worldPosition = float4(mul(World[eyeIndex], inputPosition), 1);
	precise float4x4 world4x4 = float4x4(World[eyeIndex][0], World[eyeIndex][1], World[eyeIndex][2], float4(0, 0, 0, 1));
	precise float4x4 modelView = mul(ViewProj[eyeIndex], world4x4);
	float4 viewPos = mul(modelView, inputPosition);

	vsout.Position = viewPos;

#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	vsout.Position.z += min(1, 1e-4 * max(0, viewPos.z - 70000)) * 0.5;
#	endif

	float2 uv = input.TexCoord0.xy * TexcoordOffset.zw + TexcoordOffset.xy;
#	if defined(PROJECTED_UV)
	vsout.TexCoord0.z = mul(TextureProj[eyeIndex][0], inputPosition);
	vsout.TexCoord0.w = mul(TextureProj[eyeIndex][1], inputPosition);
#	endif
	vsout.TexCoord0.xy = uv;

#	if defined(WORLD_MAP)
	vsout.InputPosition.xyz = WorldMapOverlayParameters.xyz + worldPosition.xyz;
#	endif


#	if !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(
		float3(input.Position.w, input.Normal.w * 2 - 1, input.Bitangent.w * 2 - 1),
		input.Bitangent.xyz * 2.0.xxx + -1.0.xxx,
		input.Normal.xyz * 2.0.xxx + -1.0.xxx);
	float3x3 tbnTr = transpose(tbn);

	vsout.TBN0.xyz = mul(tbn, World[eyeIndex][0].xyz);
	vsout.TBN1.xyz = mul(tbn, World[eyeIndex][1].xyz);
	vsout.TBN2.xyz = mul(tbn, World[eyeIndex][2].xyz);
	float3x3 tempTbnTr = transpose(float3x3(vsout.TBN0.xyz, vsout.TBN1.xyz, vsout.TBN2.xyz));
	tempTbnTr[0] = normalize(tempTbnTr[0]);
	tempTbnTr[1] = normalize(tempTbnTr[1]);
	tempTbnTr[2] = normalize(tempTbnTr[2]);
	tempTbnTr = transpose(tempTbnTr);
	vsout.TBN0.xyz = tempTbnTr[0];
	vsout.TBN1.xyz = tempTbnTr[1];
	vsout.TBN2.xyz = tempTbnTr[2];
#	endif

#	if defined(PROJECTED_UV)
	float3x3 texProjWorld3x3 = float3x3(World[eyeIndex][0].xyz, World[eyeIndex][1].xyz, World[eyeIndex][2].xyz);
	vsout.TexProj = mul(texProjWorld3x3, TextureProj[eyeIndex][2].xyz);
#	endif


	vsout.WorldPosition = worldPosition;
	vsout.PreviousWorldPosition = previousWorldPosition;

#	if defined(VC)
	vsout.Color = input.Color;
#	else
	vsout.Color = 1.0.xxxx;
#	endif  // VC

	float fogColorParam = min(FogParam.w,
		exp2(FogParam.z * log2(saturate(length(viewPos.xyz) * FogParam.y - FogParam.x))));

	vsout.FogParam.xyz = lerp(FogNearColor.xyz, FogFarColor.xyz, fogColorParam);
	vsout.FogParam.w = fogColorParam;


	vsout.ModelPosition = input.Position.xyz;

	return vsout;
}
#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#	undef TERRAIN_BLENDING

#if defined(DEFERRED)
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
	float4 Reflectance: SV_Target5;
	float4 Masks: SV_Target6;
};
#else
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
};
#endif

#ifdef PSHADER


SamplerState SampTerrainParallaxSampler : register(s1);


SamplerState SampColorSampler : register(s0);

#		define SampNormalSampler SampColorSampler

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
SamplerState SampSpecularSampler : register(s2);
#		endif
#		if defined(PROJECTED_UV)
SamplerState SampProjDiffuseSampler : register(s3);
#		endif


#		if defined(TRUE_PBR)
SamplerState SampParallaxSampler : register(s4);
#		endif
#		if defined(TRUE_PBR)
SamplerState SampRMAOSSampler : register(s5);
#		endif

SamplerState SampGlowSampler : register(s6);

#		if defined(PROJECTED_UV)
SamplerState SampProjNormalSampler : register(s8);
#		endif

SamplerState SampBackLightSampler : register(s9);

#		if defined(PROJECTED_UV)
SamplerState SampProjDetailSampler : register(s10);
#		endif

SamplerState SampCharacterLightProjNoiseSampler : register(s11);
SamplerState SampRimSoftLightWorldMapOverlaySampler : register(s12);

#		if defined(WORLD_MAP) && (defined(LODLANDSCAPE) || defined(LODLANDNOISE))
SamplerState SampWorldMapOverlaySnowSampler : register(s13);
#		endif


#	if defined(LODLANDNOISE)
SamplerState SampLandLodNoiseSampler : register(s15);
#	endif

SamplerState SampShadowMaskSampler : register(s14);


Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexNormalSampler : register(t1);  // normal in xyz, glossiness in w if not modelspacenormal

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
Texture2D<float4> TexSpecularSampler : register(t2);
#		endif
#		if defined(PROJECTED_UV)
Texture2D<float4> TexProjDiffuseSampler : register(t3);
#		endif


#		if defined(TRUE_PBR)
Texture2D<float4> TexParallaxSampler : register(t4);
#		endif
#		if defined(TRUE_PBR)
Texture2D<float4> TexRMAOSSampler : register(t5);
#		endif

Texture2D<float4> TexGlowSampler : register(t6);

#		if defined(PROJECTED_UV)
Texture2D<float4> TexProjNormalSampler : register(t8);
#		endif

Texture2D<float4> TexBackLightSampler : register(t9);

#		if defined(PROJECTED_UV)
Texture2D<float4> TexProjDetail : register(t10);
#		endif

Texture2D<float4> TexCharacterLightProjNoiseSampler : register(t11);
Texture2D<float4> TexRimSoftLightWorldMapOverlaySampler : register(t12);

#		if defined(WORLD_MAP) && (defined(LODLANDSCAPE) || defined(LODLANDNOISE))
Texture2D<float4> TexWorldMapOverlaySnowSampler : register(t13);
#		endif


#	if defined(LODLANDNOISE)
Texture2D<float4> TexLandLodNoiseSampler : register(t15);
#	endif

Texture2D<float4> TexShadowMaskSampler : register(t14);

cbuffer PerTechnique : register(b0)
{
	float4 FogColor : packoffset(c0);           // Color in xyz, invFrameBufferRange in w
	float4 ColourOutputClamp : packoffset(c1);  // fLightingOutputColourClampPostLit in x, fLightingOutputColourClampPostEnv in y, fLightingOutputColourClampPostSpec in z
	float4 VPOSOffset : packoffset(c2);         // ???
};

cbuffer PerMaterial : register(b1)
{
	float4 LODTexParams : packoffset(c0);  // TerrainTexOffset in xy, LodBlendingEnabled in z
	float4 TintColor : packoffset(c1);
	float4 EnvmapData : packoffset(c2);  // fEnvmapScale in x, 1 or 0 in y depending of if has envmask
	float4 ParallaxOccData : packoffset(c3);
	float4 SpecularColor : packoffset(c4);  // Shininess in w, color in xyz
	float4 SparkleParams : packoffset(c5);
	float4 MultiLayerParallaxData : packoffset(c6);  // Layer thickness in x, refraction scale in y, uv scale in zw
	float4 LightingEffectParams : packoffset(c7);  // fSubSurfaceLightRolloff in x, fRimLightPower in y
	float4 IBLParams : packoffset(c8);

#	if !defined(TRUE_PBR)
	float4 LandscapeTexture1to4IsSnow : packoffset(c9);
	float4 LandscapeTexture5to6IsSnow : packoffset(c10);  // bEnableSnowMask in z, inverse iLandscapeMultiNormalTilingFactor in w
	float4 LandscapeTexture1to4IsSpecPower : packoffset(c11);
	float4 LandscapeTexture5to6IsSpecPower : packoffset(c12);
	float4 SnowRimLightParameters : packoffset(c13);  // fSnowRimLightIntensity in x, fSnowGeometrySpecPower in y, fSnowNormalSpecPower in z, bEnableSnowRimLighting in w
#	endif


	float4 CharacterLightParams : packoffset(c14);
	// VR is [9] instead of [15]

	uint PBRFlags : packoffset(c15.x);
	float3 PBRParams1 : packoffset(c15.y);  // roughness scale, displacement scale, specular level
	float4 PBRParams2 : packoffset(c16);    // subsurface color, subsurface opacity
};

cbuffer PerGeometry : register(b2)
{
	float3 DirLightDirection : packoffset(c0);
	float3 DirLightColor : packoffset(c1);
	float4 ShadowLightMaskSelect : packoffset(c2);
	float4 MaterialData : packoffset(c3);  // envmapLODFade in x, specularLODFade in y, alpha in z
	float AlphaTestRef : packoffset(c4);
	float3 EmitColor : packoffset(c4.y);
	float4 ProjectedUVParams : packoffset(c6);
	float4 SSRParams : packoffset(c7);
	float4 WorldMapOverlayParametersPS : packoffset(c8);
	float4 ProjectedUVParams2 : packoffset(c9);
	float4 ProjectedUVParams3 : packoffset(c10);  // fProjectedUVDiffuseNormalTilingScale in x, fProjectedUVNormalDetailTilingScale in y, EnableProjectedNormals in w
	row_major float3x4 DirectionalAmbient : packoffset(c11);
	float4 AmbientSpecularTintAndFresnelPower : packoffset(c14);  // Fresnel power in z, color in xyz
	float4 PointLightPosition[7] : packoffset(c15);               // point light radius in w
	float4 PointLightColor[7] : packoffset(c22);
	float2 NumLightNumShadowLight : packoffset(c29);
};

cbuffer AlphaTestRefBuffer : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

float GetSoftLightMultiplier(float angle)
{
	float softLightParam = saturate((LightingEffectParams.x + angle) / (1 + LightingEffectParams.x));
	float arg1 = (softLightParam * softLightParam) * (3 - 2 * softLightParam);
	float clampedAngle = saturate(angle);
	float arg2 = (clampedAngle * clampedAngle) * (3 - 2 * clampedAngle);
	float softLigtMul = saturate(arg1 - arg2);
	return softLigtMul;
}

float GetRimLightMultiplier(float3 L, float3 V, float3 N)
{
	float NdotV = saturate(dot(N, V));
	return exp2(LightingEffectParams.y * log2(1 - NdotV)) * saturate(dot(V, -L));
}

#	if !defined(TRUE_PBR)
float ProcessSparkleColor(float color)
{
	return exp2(SparkleParams.y * log2(min(1, abs(color))));
}
#	endif

float3 TransformNormal(float3 normal)
{
	return normal * 2 + -1.0.xxx;
}

float GetLodLandBlendParameter(float3 color)
{
	float result = saturate(1.6666666 * (dot(color, 0.55.xxx) - 0.4));
	result = ((result * result) * (3 - result * 2));
#	if !defined(WORLD_MAP)
	result *= 0.8;
#	endif
	return result;
}

float GetLodLandBlendMultiplier(float parameter, float mask)
{
	return 0.8333333 * (parameter * (0.37 - mask) + mask) + 0.37;
}

float GetLandSnowMaskValue(float alpha)
{
#	if !defined(TRUE_PBR)
	return alpha * LandscapeTexture5to6IsSnow.z + (1 + -LandscapeTexture5to6IsSnow.z);
#	else
	return 0;
#	endif
}

float3 GetLandNormal(float landSnowMask, float3 normal, float2 uv, SamplerState sampNormal, Texture2D<float4> texNormal)
{
	float3 landNormal = TransformNormal(normal);
	return landNormal;
}




#	if defined(WORLD_MAP)
float3 GetWorldMapNormal(PS_INPUT input, float3 rawNormal, float3 baseColor)
{
	float3 normal = normalize(rawNormal);
#		if defined(MODELSPACENORMALS)
	float3 worldMapNormalSrc = normal.xyz;
#		else
	float3 worldMapNormalSrc = float3(input.TBN0.z, input.TBN1.z, input.TBN2.z);
#		endif
	float3 worldMapNormal = 7.0.xxx * (-0.2.xxx + abs(normalize(worldMapNormalSrc)));
	worldMapNormal = max(0.01.xxx, worldMapNormal * worldMapNormal * worldMapNormal);
	worldMapNormal /= dot(worldMapNormal, 1.0.xxx);
	float3 worldMapColor1 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.yz).xyz;
	float3 worldMapColor2 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.xz).xyz;
	float3 worldMapColor3 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.xy).xyz;
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float3 worldMapSnowColor1 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.yz).xyz;
	float3 worldMapSnowColor2 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.xz).xyz;
	float3 worldMapSnowColor3 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.xy).xyz;
#		endif
	float3 worldMapColor = worldMapNormal.xxx * worldMapColor1 + worldMapNormal.yyy * worldMapColor2 + worldMapNormal.zzz * worldMapColor3;
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float3 worldMapSnowColor = worldMapSnowColor1 * worldMapNormal.xxx + worldMapSnowColor2 * worldMapNormal.yyy + worldMapSnowColor3 * worldMapNormal.zzz;
	float snowMultiplier = GetLodLandBlendParameter(baseColor);
	worldMapColor = snowMultiplier * (worldMapSnowColor - worldMapColor) + worldMapColor;
#		endif
	worldMapColor = normalize(2.0.xxx * (-0.5.xxx + (worldMapColor)));
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float worldMapLandTmp = saturate(19.9999962 * (rawNormal.z - 0.95));
	worldMapLandTmp = saturate(-(worldMapLandTmp * worldMapLandTmp) * (worldMapLandTmp * -2 + 3) + 1.5);
	float3 worldMapLandTmp1 = normalize(normal.zxy * float3(1, 0, 0) - normal.yzx * float3(0, 0, 1));
	float3 worldMapLandTmp2 = normalize(worldMapLandTmp1.yzx * normal.zxy - worldMapLandTmp1.zxy * normal.yzx);
	float3 worldMapLandTmp3 = normalize(worldMapColor.xxx * worldMapLandTmp1 + worldMapColor.yyy * worldMapLandTmp2 + worldMapColor.zzz * normal.xyz);
	float worldMapLandTmp4 = dot(worldMapLandTmp3, worldMapLandTmp3);
	if (worldMapLandTmp4 > 0.999 && worldMapLandTmp4 < 1.001) {
		normal.xyz = worldMapLandTmp * (worldMapLandTmp3 - normal.xyz) + normal.xyz;
	}
#		else
	normal.xyz = normalize(
		WorldMapOverlayParametersPS.zzz * (rawNormal.xyz - worldMapColor.xyz) + worldMapColor.xyz);
#		endif
	return normal;
}

float3 GetWorldMapBaseColor(float3 originalBaseColor, float3 rawBaseColor, float texProjTmp)
{
#		if defined(LODOBJECTS) && !defined(PROJECTED_UV)
	return rawBaseColor;
#		endif
#		if defined(LODLANDSCAPE) || defined(LODOBJECTSHD) || defined(LODLANDNOISE)
	float lodMultiplier = GetLodLandBlendParameter(originalBaseColor.xyz);
#		elif defined(LODOBJECTS) && defined(PROJECTED_UV)
	float lodMultiplier = saturate(10 * texProjTmp);
#		else
	float lodMultiplier = 1;
#		endif
#		if defined(LODOBJECTS)
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.269999981, 0.281000018, 0.441000015, 0.441000015) + float4(0.0780000091, 0.09799999, -0.0349999964, 0.465000004);
	float4 lodColor = lodColorMul.xyzw * 2.0.xxxx;
	lodColor.xyz = Color::Diffuse(lodColor.xyz);
	bool useLodColorZ = lodColorMul.w > 0.5;
	lodColor.xyz = max(lodColor.xyz, rawBaseColor.xyz);
	lodColor.w = useLodColorZ ? lodColor.z : min(lodColor.w, rawBaseColor.z);
	return (0.5 * lodMultiplier).xxx * (lodColor.xyw - rawBaseColor.xyz) + rawBaseColor;
#		else
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.199999988, 0.441000015, 0.269999981, 0.281000018) + float4(0.300000012, 0.465000004, 0.0780000091, 0.09799999);
	float3 lodColor = lodColorMul.zwy * 2.0.xxx;
	lodColor.xyz = Color::Diffuse(lodColor.xyz);
	lodColor.xy = max(lodColor.xy, rawBaseColor.xy);
	lodColor.z = lodColorMul.y > 0.5 ? max((lodMultiplier * 0.441 + -0.0349999964) * 2, rawBaseColor.z) : min(lodColor.z, rawBaseColor.z);
	return lodColorMul.xxx * (lodColor - rawBaseColor.xyz) + rawBaseColor;
#		endif
}
#	endif

float GetSnowParameterY(float texProjTmp, float alpha)
{
	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::BaseObjectIsSnow) {
		return min(1, texProjTmp + alpha);
	}
	return texProjTmp;
}


#	if defined(WORLD_MAP)
#		undef CLOUD_SHADOWS
#		undef SKYLIGHTING
#	endif

#	include "Common/LightingCommon.hlsli"

#	if defined(WATER_EFFECTS)
#		include "WaterEffects/WaterCaustics.hlsli"
#	endif


#	if defined(EXTENDED_MATERIALS) && (defined(TRUE_PBR))
#		define EMAT
#	endif


#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(TRUE_PBR)
#		include "Common/PBR.hlsli"
#	endif



#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#		include "InverseSquareLighting/InverseSquareLighting.hlsli"
#	endif


#	if defined(WETNESS_EFFECTS)
#		include "WetnessEffects/WetnessEffects.hlsli"
#	endif

#	if defined(TERRAIN_BLENDING)
#		include "TerrainBlending/TerrainBlending.hlsli"
#	endif


#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif


#	if defined(TERRAIN_VARIATION)
#		include "TerrainVariation/TerrainVariation.hlsli"
#	endif

#	if defined(EXTENDED_TRANSLUCENCY) && !(defined(LODOBJECTSHD) || defined(LODOBJECTS))
#		include "ExtendedTranslucency/ExtendedTranslucency.hlsli"
#		define ANISOTROPIC_ALPHA
#	endif

#	define LinearSampler SampColorSampler

#	include "Common/ShadowSampling.hlsli"

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(EXP_HEIGHT_FOG)
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

#	include "Common/LightingEval.hlsli"

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout;
	uint eyeIndex = Stereo::GetEyeIndexPS(input.Position, VPOSOffset);

	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float3 viewDirection = -normalize(input.WorldPosition.xyz);

	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
	float screenNoise = Random::InterleavedGradientNoise(input.Position.xy, SharedData::FrameCount);

#	if defined(DEFERRED)
	const bool inWorld = true;
#	else
	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
#	endif
	const bool inReflection = Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection;

	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, viewPosition.z);

#	if 0 || !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(input.TBN0.xyz, input.TBN1.xyz, input.TBN2.xyz);

#		if 1 && 1
	// Fix incorrect vertex normals on double-sided meshes
	if (!frontFace)
		tbn = lerp(tbn, -tbn, nearFactor);
#		endif

	float3x3 tbnTr = transpose(tbn);

	tbnTr[0] = normalize(tbnTr[0]);
	tbnTr[1] = normalize(tbnTr[1]);
	tbnTr[2] = normalize(tbnTr[2]);

	tbn = transpose(tbnTr);

#	endif  // defined (SKINNED) || !defined (MODELSPACENORMALS)

#	if !defined(TRUE_PBR)
	float shininess = 0.0;
#	endif

#	if defined(TERRAIN_BLENDING)
	float blendFactorTerrain = 0.0;
	[flatten] if (SharedData::terrainBlendingSettings.Enabled)
	{
		float depthSampled = TerrainBlending::TerrainBlendingMaskTexture[input.Position.xy].x;

		float depthSampledLinear = SharedData::GetScreenDepth(depthSampled);
		float depthPixelLinear = SharedData::GetScreenDepth(input.Position.z);

		blendFactorTerrain = saturate((depthSampledLinear - depthPixelLinear) / 5.0);

		if (input.Position.z == depthSampled)
			blendFactorTerrain = 1;
	}
#	endif

	float2 uv = input.TexCoord0.xy;
	float2 uvOriginal = uv;


	float mipLevel = 0;
	float sh0 = 0;
	float pixelOffset = 0;


	float curvature = 0;
	float normalSmoothness = 0;

#	if !defined(MODELSPACENORMALS)
	float3 vertexNormal = tbnTr[2];
#	endif

	float3 entryNormal = 0;
	float3 entryNormalTS = 0;
	float eta = 1;
	float3 refractedViewDirection = viewDirection;
	float4 sampledCoatColor = PBRParams2;
	float3 complexSpecular = 1.0;  // Declare complexSpecular at a higher scope so it's available throughout the shader (NEEDED FOR STOCH. FIX)


	bool useSnowSpecular = false;

#	if 0 || !defined(PROJECTED_UV)
	bool useSnowDecalSpecular = true;
#	else
	bool useSnowDecalSpecular = false;
#	endif  // defined(SPARKLE) || !defined(PROJECTED_UV)

	float2 diffuseUv = uv;



	diffuseUv = uv;

	float4 baseColor = 0;
	float4 normal = 0;
	float glossiness = 0;

	float4 rawRMAOS = 0;

	float4 glintParameters = 0;


	float4 rawBaseColor = TexColorSampler.SampleBias(SampColorSampler, diffuseUv, SharedData::MipBias);
	baseColor = float4(Color::Diffuse(rawBaseColor.rgb), rawBaseColor.a);
	float4 normalColor = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
	normal = normalColor;
#		if defined(TRUE_PBR)
	rawRMAOS = TexRMAOSSampler.SampleBias(SampRMAOSSampler, diffuseUv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
	if ((PBRFlags & PBR::Flags::Glint) != 0) {
		glintParameters = MultiLayerParallaxData;
	}
#		endif

#	if defined(LOD_BLENDING)
#		if defined(LODOBJECTS) || defined(LODOBJECTSHD)
	baseColor.xyz = pow(abs(baseColor.xyz), SharedData::lodBlendingSettings.LODObjectGamma) * SharedData::lodBlendingSettings.LODObjectBrightness;
#		elif defined(LODLANDSCAPE)
// First apply terrain variation if enabled
#			if defined(TERRAIN_VARIATION)
	if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix) {
		float2 dx = ddx(uv);
		float2 dy = ddy(uv);
		StochasticOffsets lodOffset = ComputeStochasticOffsetsLOD(uv);
		float4 lodStochasticColor = StochasticSampleLOD(screenNoise, TexColorSampler, SampColorSampler, uv, lodOffset, dx, dy);

		// Apply the stochastic result directly
		baseColor.xyz = Color::Diffuse(lodStochasticColor.rgb);
	}
#			endif
	baseColor.xyz = pow(abs(baseColor.xyz), SharedData::lodBlendingSettings.LODTerrainGamma) * SharedData::lodBlendingSettings.LODTerrainBrightness;
#		endif
#	endif  // LOD_BLENDING

	float landSnowMask1 = GetLandSnowMaskValue(baseColor.w);

#	if defined(MODELSPACENORMALS)
#		if defined(LODLANDNOISE)
	normal.xyz = normal.xzy - 0.5.xxx;
	float lodLandNoiseParameter = GetLodLandBlendParameter(baseColor.xyz);
	float noise = TexLandLodNoiseSampler.Sample(SampLandLodNoiseSampler, uv * 3.0.xx).x;
	float lodLandNoiseMultiplier = GetLodLandBlendMultiplier(lodLandNoiseParameter, noise);
	baseColor.xyz *= lodLandNoiseMultiplier;
	normal.xyz *= 2;
	normal.w = 1;
	glossiness = 0;
#		elif defined(LODLANDSCAPE)
	normal.xyz = 2.0.xxx * (-0.5.xxx + normal.xzy);
	normal.w = 1;
	glossiness = 0;
#		else
	normal.xyz = normal.xzy * 2.0.xxx + -1.0.xxx;
	normal.w = 1;
	glossiness = TexSpecularSampler.Sample(SampSpecularSampler, uv).x;
#		endif  // LODLANDNOISE
#	else
	normal.xyz = TransformNormal(normal.xyz);
	glossiness = normal.w;
#	endif  // MODELSPACENORMALS

#	if defined(WORLD_MAP)
	normal.xyz = GetWorldMapNormal(input, normal.xyz, rawBaseColor.xyz);
#	endif  // WORLD_MAP









	uint numLights = min(7, uint(NumLightNumShadowLight.x));
	uint numShadowLights = min(4, uint(NumLightNumShadowLight.y));

#	if defined(MODELSPACENORMALS)
	float3 worldNormal = normal.xyz;
	float3x3 tbnTr = ReconstructTBN(input.WorldPosition.xyz, worldNormal, screenUV);
#	else
	float3 worldNormal = normalize(mul(tbn, normal.xyz));

#	endif      // defined (MODELSPACENORMALS) && !defined (SKINNED)

	float2 baseShadowUV = 1.0.xx;
	float4 shadowColor = 1.0;
	if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir) || inWorld) || numShadowLights > 0) {
		baseShadowUV = input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy;
		float2 adjustedShadowUV = baseShadowUV * VPOSOffset.xy + VPOSOffset.zw;
		float2 shadowUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(adjustedShadowUV);
		shadowColor = TexShadowMaskSampler.Sample(SampShadowMaskSampler, shadowUV);
	}

	float projectedMaterialWeight = 0;

	float projWeight = 0;

#	if defined(PROJECTED_UV)
	float3 projWorldPos = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
	float3 triFaceNormal = normalize(-cross(ddx(input.WorldPosition.xyz), ddy(input.WorldPosition.xyz)));
	float3 triWeights = Triplanar::GetWeights(tbnTr[2], triFaceNormal);
	float projNoise = Triplanar::Sample(TexCharacterLightProjNoiseSampler, SampCharacterLightProjNoiseSampler, projWorldPos, triWeights, ProjectedUVParams.z).x;
	float3 texProj = normalize(input.TexProj);
#		if defined(LODOBJECTSHD)
	float vertexAlpha = 1;
#		else
	float vertexAlpha = input.Color.w;
#		endif  // defined (TREE_ANIM) || defined (LODOBJECTSHD)
	projWeight = -ProjectedUVParams.x * projNoise + (dot(worldNormal.xyz, texProj) * vertexAlpha - ProjectedUVParams.w);
#		if defined(LODOBJECTSHD)
	projWeight += (-0.5 + input.Color.w) * 2.5;
#		endif  // LODOBJECTSHD
#		if 1 && 1 && 1 && 1
	if (ProjectedUVParams3.w > 0.5) {
		float diffuseNormalScale = ProjectedUVParams3.x * ProjectedUVParams.z;
		float3 projNormal = TransformNormal(Triplanar::SampleStochastic(TexProjNormalSampler, SampProjNormalSampler, projWorldPos, triWeights, diffuseNormalScale, screenNoise).xyz);
		float detailNormalScale = ProjectedUVParams3.y * ProjectedUVParams.z;
		float3 projDetailNormal = Triplanar::SampleStochastic(TexProjDetail, SampProjDetailSampler, projWorldPos, triWeights, detailNormalScale, screenNoise).xyz;
		float3 finalProjNormal = normalize(TransformNormal(projDetailNormal) * float3(1, 1, projNormal.z) + float3(projNormal.xy, 0));
		float3 projBaseColor = Color::ColorToLinear(Triplanar::SampleStochastic(TexProjDiffuseSampler, SampProjDiffuseSampler, projWorldPos, triWeights, diffuseNormalScale, screenNoise).xyz) * Color::ColorToLinear(ProjectedUVParams2.xyz);
		projectedMaterialWeight = smoothstep(0, 1, 5 * (0.1 + projWeight));
#			if defined(TRUE_PBR)
		projBaseColor = max(0, EnvmapData.x * projBaseColor);
		rawRMAOS.xyw = lerp(rawRMAOS.xyw, float3(ParallaxOccData.x, 0, ParallaxOccData.y), projectedMaterialWeight);
		float4 projectedGlintParameters = 0;
		if ((PBRFlags & PBR::Flags::ProjectedGlint) != 0) {
			projectedGlintParameters = SparkleParams;
		}
		glintParameters = lerp(glintParameters, projectedGlintParameters, projectedMaterialWeight);
#			else
		projBaseColor *= Color::VanillaDiffuseColorMult();
#			endif  // TRUE_PBR
#			if defined(LOD_BLENDING) && (defined(LODOBJECTS) || defined(LODOBJECTSHD))
		projBaseColor.xyz = pow(abs(projBaseColor.xyz), SharedData::lodBlendingSettings.LODObjectSnowGamma) * SharedData::lodBlendingSettings.LODObjectSnowBrightness;
#			endif  // LOD_BLENDING
		normal.xyz = lerp(normal.xyz, finalProjNormal, projectedMaterialWeight);
		baseColor.xyz = lerp(baseColor.xyz, projBaseColor, projectedMaterialWeight);

	} else {
		if (projWeight > 0) {
			baseColor.xyz = Color::Diffuse(ProjectedUVParams2.xyz);
		} else {
		}
	}

#		endif      // SPARKLE

#	endif      // SNOW

#	if defined(WORLD_MAP)
	baseColor.xyz = GetWorldMapBaseColor(rawBaseColor.xyz, baseColor.xyz, projWeight);
#	endif  // WORLD_MAP

#	if defined(MODELSPACENORMALS)
	float3 vertexNormal = worldNormal;
#	endif

	float3 screenSpaceNormal = normalize(FrameBuffer::WorldToView(worldNormal, false, eyeIndex));


	MaterialProperties material = (MaterialProperties)0;

	material.F0 = 0;
	material.Roughness = 1;

#	if defined(TRUE_PBR)
	material.Noise = screenNoise;

	material.Roughness = clamp(rawRMAOS.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
	material.Metallic = saturate(rawRMAOS.y);
	material.AO = rawRMAOS.z;

	// Apply vertex color to base color so PBR metals use it
	float3 pbrVertexColor = Color::SrgbToLinear(input.Color.xyz);

	if (!SharedData::linearLightingSettings.enableLinearLighting) {
		baseColor.xyz = Color::SrgbToLinear(baseColor.xyz) * pbrVertexColor;
		material.F0 = lerp(rawRMAOS.w, baseColor.xyz, material.Metallic);
		baseColor.xyz = Color::LinearToSrgb(baseColor.xyz);
	} else {
		baseColor.xyz *= pbrVertexColor;
		material.F0 = lerp(rawRMAOS.w, baseColor.xyz, material.Metallic);
	}

	material.GlintScreenSpaceScale = max(1, glintParameters.x);
	material.GlintLogMicrofacetDensity = clamp(PBR::Constants::MaxGlintDensity - glintParameters.y, PBR::Constants::MinGlintDensity, PBR::Constants::MaxGlintDensity);
	material.GlintMicrofacetRoughness = clamp(glintParameters.z, PBR::Constants::MinGlintRoughness, PBR::Constants::MaxGlintRoughness);
	material.GlintDensityRandomization = clamp(glintParameters.w, PBR::Constants::MinGlintDensityRandomization, PBR::Constants::MaxGlintDensityRandomization);


	baseColor.xyz *= 1 - material.Metallic;

	material.BaseColor = baseColor.xyz;

	float3 coatWorldNormal = worldNormal;

#		if 1 && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::Subsurface) != 0)
	{
		material.SubsurfaceColor = PBRParams2.xyz;
		material.Thickness = PBRParams2.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
		{
			float4 sampledSubsurfaceProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);

			// If LL is off, Diffuse returns sRGB
			material.SubsurfaceColor *= Color::Diffuse(sampledSubsurfaceProperties.xyz);

			if (!SharedData::linearLightingSettings.enableLinearLighting) {
				material.SubsurfaceColor = Color::LinearToSrgb(
					Color::SrgbToLinear(material.SubsurfaceColor) * pbrVertexColor);
			} else {
				material.SubsurfaceColor *= pbrVertexColor;
			}

			material.Thickness *= sampledSubsurfaceProperties.w;
		}
		material.Thickness = lerp(material.Thickness, 1, projectedMaterialWeight);
	}
	else if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		material.CoatColor = sampledCoatColor.xyz;
		material.CoatStrength = sampledCoatColor.w;
		material.CoatRoughness = MultiLayerParallaxData.x;
		material.CoatF0 = MultiLayerParallaxData.y;

		float2 coatUv = uv;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			coatUv = uvOriginal;
		}
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledCoatProperties = TexBackLightSampler.Sample(SampBackLightSampler, coatUv);
			material.CoatRoughness *= sampledCoatProperties.w;
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				coatWorldNormal = normalize(mul(tbn, TransformNormal(sampledCoatProperties.xyz)));
			}
		}
		material.CoatStrength = lerp(material.CoatStrength, 0, projectedMaterialWeight);
	}

	[branch] if ((PBRFlags & PBR::Flags::Fuzz) != 0)
	{
		material.FuzzColor = MultiLayerParallaxData.xyz;
		material.FuzzWeight = MultiLayerParallaxData.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledFuzzProperties = TexBackLightSampler.Sample(SampBackLightSampler, uv);
			material.FuzzColor *= Color::Diffuse(sampledFuzzProperties.xyz);
			material.FuzzWeight *= sampledFuzzProperties.w;
		}
		material.FuzzWeight = lerp(material.FuzzWeight, 0, projectedMaterialWeight);
	}
#		endif
#	else
	material.BaseColor = baseColor.xyz;
	material.Shininess = 0;
	material.Glossiness = 0;
	material.SpecularColor = 0;
#	endif  // TRUE_PBR


	bool dynamicCubemap = false;


	float porosity = 1.0;

#	if defined(SKYLIGHTING)
	float3 positionMSSkylight = input.WorldPosition.xyz;
#		if defined(DEFERRED)
	sh2 skylightingSH = Skylighting::Sample(positionMSSkylight, worldNormal);
#		else
	sh2 skylightingSH = inWorld ? Skylighting::Sample(positionMSSkylight, worldNormal) : Skylighting::UNIT_SH;
#		endif

#	endif

	float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz, eyeIndex);
	float waterHeight = waterData.w;

	float waterRoughnessSpecular = 1;

#	if defined(WETNESS_EFFECTS)
	// Initialize wetness parameters
	float wetness = 0.0;
	float3 wetnessNormal = vertexNormal.xyz;

	// Calculate shore wetness factors
	float wetnessDistToWater = abs(input.WorldPosition.z - waterHeight);
	float shoreFactor = saturate(1.0 - (wetnessDistToWater / SharedData::wetnessEffectsSettings.ShoreRange));
	float shoreFactorAlbedo = (input.WorldPosition.z < waterHeight) ? 1.0 : shoreFactor;

	// Calculate wetness angle and occlusion
	float minWetnessValue = SharedData::wetnessEffectsSettings.MinRainWetness;
	float minWetnessAngle = saturate(max(minWetnessValue, vertexNormal.z));
#		if defined(SKYLIGHTING)
	float wetnessOcclusion = inWorld ? saturate(SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1))) : 0.0;
#		else
	float wetnessOcclusion = inWorld;
#		endif
	float flatnessAmount = smoothstep(SharedData::wetnessEffectsSettings.PuddleMaxAngle, 1.0, minWetnessAngle);
	// Calculate raindrop effects
	float4 raindropInfo = float4(0, 0, 1, 0);
	bool shouldCalculateRaindrops = (worldNormal.z > 0.0) &&
	                                (SharedData::wetnessEffectsSettings.Raining > 0.0) &&
	                                (SharedData::wetnessEffectsSettings.EnableRaindropFx) &&
	                                (wetnessOcclusion > 0.5);

	if (shouldCalculateRaindrops) {
#		if defined(DEFERRED)
		float3 ripplePosition = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
#		else
		float3 ripplePosition = !FrameBuffer::FrameParams.y ? input.ModelPosition.xyz : input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
#		endif
		raindropInfo = WetnessEffects::GetRainDrops(ripplePosition, SharedData::wetnessEffectsSettings.Time, wetnessNormal, flatnessAmount);
	}

	// Calculate different wetness types
	float rainWetness = SharedData::wetnessEffectsSettings.Wetness * minWetnessAngle * SharedData::wetnessEffectsSettings.MaxRainWetness;
	rainWetness = max(rainWetness, raindropInfo.w);


	float shoreWetness = shoreFactor * SharedData::wetnessEffectsSettings.MaxShoreWetness;
	wetness = max(shoreWetness, rainWetness);

	// Calculate puddle effects
	float puddleWetness = SharedData::wetnessEffectsSettings.PuddleWetness * minWetnessAngle;
	float puddle = wetness;

	if (wetness > 0.0 || puddleWetness > 0.0) {
		float3 puddleCoords = ((input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz) * 0.5 + 0.5) * 0.01 / SharedData::wetnessEffectsSettings.PuddleRadius;
		puddle = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
		puddle = puddle * ((minWetnessAngle / SharedData::wetnessEffectsSettings.PuddleMaxAngle) * SharedData::wetnessEffectsSettings.MaxPuddleWetness * 0.25) + 0.5;
		puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
	}

	// Apply occlusion and distance factors
	puddle *= saturate(wetnessOcclusion * 2.0) * nearFactor;
	wetnessNormal = lerp(worldNormal.xyz, wetnessNormal, saturate(puddle));

	// Calculate wetness glossiness factors
	float wetnessGlossinessAlbedo = max(puddle, shoreFactorAlbedo * SharedData::wetnessEffectsSettings.MaxShoreWetness);
	wetnessGlossinessAlbedo *= wetnessGlossinessAlbedo;

	float wetnessGlossinessSpecular = puddle;
	if (input.WorldPosition.z < waterHeight) {
		wetnessGlossinessSpecular *= shoreFactor;
	}

	// Update flatness and normal calculations
	flatnessAmount *= smoothstep(SharedData::wetnessEffectsSettings.PuddleMinWetness, 1.0, wetnessGlossinessSpecular);

	// Apply ripple normal effects
	float3 rippleNormal = normalize(lerp(float3(0, 0, 1), raindropInfo.xyz, lerp(flatnessAmount, 1.0, 0.5)));
	wetnessNormal = WetnessEffects::ReorientNormal(rippleNormal, wetnessNormal);

	waterRoughnessSpecular = saturate(1.0 - wetnessGlossinessSpecular);
#	endif

	float llDirLightMult = SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear && (inWorld || inReflection) && !SharedData::InInterior ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;

#	if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		dirLightColor *= ExponentialHeightFog::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz);
	}
#	endif

#	if defined(WATER_EFFECTS)
	dirLightColor *= WaterEffects::ComputeCaustics(waterData, input.WorldPosition.xyz, eyeIndex);
#	endif

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (inWorld || inReflection)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);

	float dirLightAngle = dot(worldNormal.xyz, DirLightDirection.xyz);

	float3 refractedDirLightDirection = DirLightDirection;
#	if defined(TRUE_PBR) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		if (dot(DirLightDirection, coatWorldNormal) > 0)
			refractedDirLightDirection = -refract(-DirLightDirection, coatWorldNormal, eta);
	}
#	endif

	float dirSoftShadow = 1.0;
	float dirVSMDetailedShadow = 1.0;

#	if defined(VOLUMETRIC_SHADOWS)
	if (inWorld && !inReflection && !SharedData::InInterior)
		dirSoftShadow = ShadowSampling::GetLightingShadow(input.WorldPosition.xyz, eyeIndex, dirVSMDetailedShadow);
#	endif

	float dirDetailedShadow = 1.0;

	if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir)) {
		dirDetailedShadow *= shadowColor.x;

#	if !defined(VOLUMETRIC_SHADOWS)
		dirSoftShadow = dirDetailedShadow;
#	endif
	} else {
		dirDetailedShadow = dirVSMDetailedShadow;
	}




	float3 diffuseColor = 0.0.xxx;
	float3 specularColor = 0.0.xxx;
	float3 transmissionColor = 0.0.xxx;

	float3 lightsDiffuseColor = 0.0.xxx;
	float3 coatLightsDiffuseColor = 0.0.xxx;
	float3 lightsSpecularColor = 0.0.xxx;

	float3 lodLandDiffuseColor = 0;

	// Directiontal Lighting
	DirectContext dirLightContext;
	DirectLightingOutput dirLightOutput;
#	if defined(TRUE_PBR)
	dirLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedDirLightDirection, DirLightDirection, dirLightColor, dirDetailedShadow, dirSoftShadow);
#	else
	dirLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, DirLightDirection, dirLightColor, dirDetailedShadow, dirSoftShadow);
#	endif

	float2 uvOriginal_ddx = ddx(uvOriginal);
	float2 uvOriginal_ddy = ddy(uvOriginal);
	EvaluateLighting(dirLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, dirLightOutput);
#	if defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1)
		EvaluateWetnessLighting(wetnessNormal, dirLightContext, waterRoughnessSpecular, dirLightOutput);
#	endif

	lightsDiffuseColor += dirLightOutput.diffuse;
	lightsSpecularColor += dirLightOutput.specular;
#	if defined(TRUE_PBR)
	coatLightsDiffuseColor += dirLightOutput.coatDiffuse;
#	endif
	transmissionColor += dirLightOutput.transmission;

#		if !defined(LIGHT_LIMIT_FIX)
	[loop] for (uint lightIndex = 0; lightIndex < numLights; lightIndex++)
	{
		float3 lightDirection = PointLightPosition[eyeIndex * numLights + lightIndex].xyz - input.WorldPosition.xyz;
		float lightDist = length(lightDirection);
		float intensityFactor = saturate(lightDist / PointLightPosition[lightIndex].w);
		if (intensityFactor == 1)
			continue;

		float intensityMultiplier = 1 - intensityFactor * intensityFactor;
		float3 lightColor = Color::PointLight(PointLightColor[lightIndex].xyz) * intensityMultiplier;
		float lightShadow = 1.f;
		if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) {
			if (lightIndex < numShadowLights) {
				lightShadow *= shadowColor[ShadowLightMaskSelect[lightIndex]];
			}
		}

		float3 normalizedLightDirection = normalize(lightDirection);

		DirectContext pointLightContext;
		DirectLightingOutput pointLightOutput;
#			if defined(TRUE_PBR)
		{
			float3 refractedLightDirection = normalizedLightDirection;
#				if 1 && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
			{
				if (dot(normalizedLightDirection, coatWorldNormal) > 0)
					refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
			}
#				endif
			pointLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightColor, lightShadow, lightShadow);
		}
#			else
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, normalizedLightDirection, lightColor, lightShadow, lightShadow);
#			endif
		EvaluateLighting(pointLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, pointLightOutput);
#			if defined(WETNESS_EFFECTS)
		if (waterRoughnessSpecular < 1)
			EvaluateWetnessLighting(wetnessNormal, pointLightContext, waterRoughnessSpecular, pointLightOutput);
#			endif
		lightsDiffuseColor += pointLightOutput.diffuse;
		lightsSpecularColor += pointLightOutput.specular;
#			if defined(TRUE_PBR)
		coatLightsDiffuseColor += pointLightOutput.coatDiffuse;
#			endif
		transmissionColor += pointLightOutput.transmission;
	}

#		else

	uint numClusteredLights = 0;
	uint totalLightCount = LightLimitFix::NumStrictLights;
	uint clusterIndex = 0;
	uint lightOffset = 0;
	if (inWorld && LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		numClusteredLights = LightLimitFix::lightGrid[clusterIndex].lightCount;
		totalLightCount += numClusteredLights;
		lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
	}

	[loop] for (uint lightIndex = 0; lightIndex < totalLightCount; lightIndex++)
	{
		LightLimitFix::Light light;
		if (lightIndex < LightLimitFix::NumStrictLights) {
			light = LightLimitFix::StrictLights[lightIndex];
		} else {
			uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + (lightIndex - LightLimitFix::NumStrictLights)];
			light = LightLimitFix::lights[clusteredLightIndex];

			if (LightLimitFix::IsLightIgnored(light))
				continue;
		}

		float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
		float lightDist = length(lightDirection);

#			if defined(ISL)
		float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
		if (intensityMultiplier < 1e-5)
			continue;
#			else
		float intensityFactor = saturate(lightDist / light.radius);
		if (intensityFactor == 1)
			continue;
		float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#			endif

		const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
		float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * intensityMultiplier * light.fade;
		float lightShadow = 1.0;

		float shadowComponent = 1.0;
		if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) {
			if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
				shadowComponent = shadowColor[light.shadowLightIndex];
				lightShadow *= shadowComponent;
			}
		}

		float3 normalizedLightDirection = normalize(lightDirection);
		float lightAngle = dot(worldNormal.xyz, normalizedLightDirection.xyz);

		float3 refractedLightDirection = normalizedLightDirection;
#			if defined(TRUE_PBR) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			if (dot(normalizedLightDirection, coatWorldNormal) > 0)
				refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
		}
#			endif

		float parallaxShadow = 1;


		DirectContext pointLightContext;
		DirectLightingOutput pointLightOutput;
		float pointLightShadow = lightShadow * parallaxShadow;
#			if defined(TRUE_PBR)
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightColor, pointLightShadow, pointLightShadow);
#			else
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, normalizedLightDirection, lightColor, pointLightShadow, pointLightShadow);
#			endif
		EvaluateLighting(pointLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, pointLightOutput);
#			if defined(WETNESS_EFFECTS)
		if (waterRoughnessSpecular < 1)
			EvaluateWetnessLighting(wetnessNormal, pointLightContext, waterRoughnessSpecular, pointLightOutput);
#			endif

		lightsDiffuseColor += pointLightOutput.diffuse;
		lightsSpecularColor += pointLightOutput.specular;
#			if defined(TRUE_PBR)
		coatLightsDiffuseColor += pointLightOutput.coatDiffuse;
#			endif
		transmissionColor += pointLightOutput.transmission;
	}
#		endif

	diffuseColor += lightsDiffuseColor;
	specularColor += lightsSpecularColor;

	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::CharacterLight) {
		float charLightMul = saturate(dot(viewDirection, worldNormal.xyz)) * CharacterLightParams.x + CharacterLightParams.y * saturate(dot(float2(0.164398998, -0.986393988), worldNormal.yz));
		float charLightColor = min(CharacterLightParams.w, max(0, CharacterLightParams.z * TexCharacterLightProjNoiseSampler.Sample(SampCharacterLightProjNoiseSampler, baseShadowUV).x));
		diffuseColor += (charLightMul * charLightColor).xxx;
	}


	// sRGB by default, linear if LL on
	float3 emitColor = Color::EmitColor(EmitColor);
#	if 1 && !defined(LODLANDSCAPE)
	bool hasEmissive = (0x3F & (Permutation::PixelShaderDescriptor >> 24)) == Permutation::LightingTechnique::Glowmap;
#		if defined(TRUE_PBR)
	hasEmissive = hasEmissive || (PBRFlags & PBR::Flags::HasEmissive != 0);
#		endif
	[branch] if (hasEmissive)
	{
		// Input TexGlowSampler = linear by default, but Color::Glowmap returns in sRGB if LL disabled
		float3 glowColor = Color::Glowmap(TexGlowSampler.Sample(SampGlowSampler, uv).xyz);

#		if defined(TRUE_PBR)
		float3 vertexColor = Color::SrgbToLinear(input.Color.xyz);

		if (!SharedData::linearLightingSettings.enableLinearLighting) {
			emitColor = Color::SrgbToLinear(emitColor);
			glowColor = Color::SrgbToLinear(glowColor);
			emitColor *= glowColor;
			emitColor *= vertexColor;
			emitColor = Color::LinearToSrgb(emitColor);
		} else {
			emitColor *= glowColor;
			emitColor *= vertexColor;
		}
#		else
		if (!SharedData::linearLightingSettings.enableLinearLighting) {
			emitColor = Color::LinearToSrgb(Color::SrgbToLinear(emitColor) * Color::SrgbToLinear(glowColor));
		} else {
			emitColor *= glowColor;
		}
#		endif  // TRUE_PBR
	}
#	endif

#	if !defined(TRUE_PBR)
	diffuseColor += emitColor.xyz;
#	endif

	IndirectContext indirectContext = (IndirectContext)0;
	IndirectLobeWeights indirectLobeWeights;

	float3 ambientNormal = worldNormal.xyz;

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(ambientNormal)));

#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		if (SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection) {
			directionalAmbientColor = ImageBasedLighting::GetStaticDiffuseIBL(ambientNormal, SampColorSampler);
		}
	}
#	endif

#	if defined(SKYLIGHTING)
	float skylightingDiffuse = 1;
	float skylightingFadeOutFactor = 1.0;
	if (!SharedData::InInterior) {
		skylightingFadeOutFactor = Skylighting::GetFadeOutFactor(input.WorldPosition.xyz);
		skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, ambientNormal, skylightingFadeOutFactor);
	}
#	endif

#	if defined(SKYLIGHTING)
	float3 vertexColor = input.Color.xyz;
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	// Modify skylightingDiffuse such that skylightingDiffuse * vertexAO = min(skylightingDiffuse, vertexAO)
	skylightingDiffuse = saturate(skylightingDiffuse / max(vertexAO, 1e-5));
#		if defined(TRUE_PBR)
	vertexColor = 1;
#		endif
#	else
#		if defined(TRUE_PBR)
	float3 vertexColor = 1;
#		else
	float3 vertexColor = input.Color.xyz;
#		endif
#	endif  // defined (HAIR)

#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		if (!(SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection)) {
#		if defined(SKYLIGHTING)
			directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -ambientNormal, skylightingDiffuse);
#		else
			directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -ambientNormal);
#		endif
		}
	}
#	endif

	float3 reflectionDiffuseColor = diffuseColor + directionalAmbientColor;


	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);

#	if defined(WETNESS_EFFECTS)
#		if !(0 || 0 || 0)
#			if defined(TRUE_PBR)
	[branch] if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		porosity = 0;
	}
	else
	{
		porosity = lerp(porosity, 0.0, saturate(sqrt(material.Metallic)));
	}
#			endif
	float wetnessDarkeningAmount = porosity * wetnessGlossinessAlbedo;
	material.BaseColor = lerp(material.BaseColor, pow(abs(material.BaseColor), 1.0 + wetnessDarkeningAmount), 0.5);
#		endif
#	endif

	float4 color = 0;

	indirectContext = CreateIndirectLightingContext(ambientNormal, vertexNormal.xyz, viewDirection);

	GetIndirectLobeWeights(indirectLobeWeights, indirectContext, material, uvOriginal);

#	if defined(WETNESS_EFFECTS)
#		if defined(DYNAMIC_CUBEMAPS)
	float3 wetnessReflectance = GetWetnessIndirectLobeWeights(indirectLobeWeights, wetnessNormal, waterRoughnessSpecular, indirectContext);
#		else
	float3 wetnessReflectance = 0.0;
#		endif
#	endif


#	if defined(TRUE_PBR)
	{
		float3 directLightsDiffuseInput = diffuseColor * material.BaseColor;
		[branch] if ((PBRFlags & PBR::Flags::ColoredCoat) != 0)
		{
			directLightsDiffuseInput = lerp(directLightsDiffuseInput, material.CoatColor * coatLightsDiffuseColor, material.CoatStrength);
		}

		color.xyz += directLightsDiffuseInput;
	}

	// Fixes white items in UI for VR
	[branch] if ((PBRFlags & PBR::Flags::HasEmissive) != 0)
	{
		color.xyz += emitColor.xyz;
	}
#	else
	color.xyz += diffuseColor * material.BaseColor;
#	endif

	color.xyz += indirectLobeWeights.diffuse * directionalAmbientColor;
	color.xyz += transmissionColor;

	color.xyz *= vertexColor;



	diffuseColor = reflectionDiffuseColor;



#	if defined(TRUE_PBR)
	color.xyz *= Color::PBRLightingScale;
	specularColor *= Color::PBRLightingScale;
	indirectLobeWeights.diffuse *= Color::PBRLightingScale;
#	endif

	float3 outputAlbedo = indirectLobeWeights.diffuse * vertexColor.xyz;

	directionalAmbientColor *= outputAlbedo;

#	if defined(SKYLIGHTING)
#		if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#		endif
	{
		Skylighting::ApplySkylighting(color.xyz, directionalAmbientColor, outputAlbedo, skylightingDiffuse);
	}
#	endif

#	if !defined(DEFERRED)
	color.xyz = Color::IrradianceToLinear(color.xyz);
	color.xyz += specularColor;

	if (any(indirectLobeWeights.specular > 0)
#		if defined(WETNESS_EFFECTS)
		|| any(wetnessReflectance > 0)
#		endif
	)
#		if defined(DYNAMIC_CUBEMAPS)
#			if defined(SKYLIGHTING)
		color.xyz += indirectLobeWeights.specular * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(worldNormal, viewDirection, material.Roughness, skylightingSH);
#				if defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1)
		color.xyz += wetnessReflectance * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(wetnessNormal, viewDirection, waterRoughnessSpecular, skylightingSH);
#				endif
#			else
		color.xyz += indirectLobeWeights.specular * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(worldNormal, viewDirection, material.Roughness);
#				if defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1)
		color.xyz += wetnessReflectance * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(wetnessNormal, viewDirection, waterRoughnessSpecular);
#				endif
#			endif
#		else
		color.xyz += indirectLobeWeights.specular * directionalAmbientColor;
#		endif

	color.xyz = Color::IrradianceToGamma(color.xyz);
	float3 fogColor = Color::Fog(input.FogParam.xyz);
	float fogFactor = Color::FogAlpha(input.FogParam.w);
#		if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		fogColor = ImageBasedLighting::GetFogIBLColor(fogColor);
	}
#		endif
#		if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		float4 exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFog(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, fogColor);
		fogColor = exponentialHeightFog.xyz;
		fogFactor = exponentialHeightFog.w;
	}
#		endif
	if (FrameBuffer::FrameParams.y && FrameBuffer::FrameParams.z)
		color.xyz = lerp(color.xyz, fogColor, fogFactor);
#	endif


	float alpha = baseColor.w;
#		if defined(DO_ALPHA_TEST)
	[branch] if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::AdditionalAlphaMask) != 0)
	{
		uint2 alphaMask = input.Position.xy;
		alphaMask.x = ((alphaMask.x << 2) & 12);
		alphaMask.x = (alphaMask.y & 3) | (alphaMask.x & ~3);
		const float maskValues[16] = {
			0.003922,
			0.533333,
			0.133333,
			0.666667,
			0.800000,
			0.266667,
			0.933333,
			0.400000,
			0.200000,
			0.733333,
			0.066667,
			0.600000,
			0.996078,
			0.466667,
			0.866667,
			0.333333,
		};

		float testTmp = 0;
		if (MaterialData.z - maskValues[alphaMask.x] < 0) {
			discard;
		}
	}
	else
#		endif  // defined(DO_ALPHA_TEST)
	{
		alpha *= MaterialData.z;
	}
#		if !(defined(LODOBJECTSHD) || defined(LODOBJECTS))
	alpha *= input.Color.w;
#		endif  // !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
#		if defined(DO_ALPHA_TEST)
	if (alpha - AlphaTestRefRS < 0) {
		discard;
	}
#		endif      // DO_ALPHA_TEST


	psout.Diffuse.w = alpha;

	psout.Diffuse.xyz = color.xyz;

	psout.MotionVectors.xy = screenMotionVector.xy;
	psout.MotionVectors.zw = float2(0, psout.Diffuse.w);

#	if defined(DEFERRED)

#		if defined(TERRAIN_BLENDING)
	[flatten] if (SharedData::terrainBlendingSettings.Enabled)
	{
		psout.Diffuse.w = blendFactorTerrain;
	}
#		endif

	psout.MotionVectors.zw = float2(0.0, psout.Diffuse.w);
	psout.Specular = float4(specularColor, psout.Diffuse.w);
	psout.Albedo = float4(outputAlbedo, psout.Diffuse.w);

#		if defined(WETNESS_EFFECTS)
	indirectLobeWeights.specular += wetnessReflectance;
	if (waterRoughnessSpecular < 1) {
		screenSpaceNormal = lerp(screenSpaceNormal, normalize(FrameBuffer::WorldToView(wetnessNormal, false, eyeIndex)), saturate(wetnessGlossinessSpecular));
		material.Roughness = lerp(material.Roughness, waterRoughnessSpecular, wetnessGlossinessSpecular);
	}
#		endif

	psout.Reflectance = float4(indirectLobeWeights.specular, psout.Diffuse.w);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), saturate(1.0 - material.Roughness), psout.Diffuse.w);



	float masksZ = Color::RGBToYCoCg(directionalAmbientColor).x;

	psout.Masks = float4(0, 0, masksZ, psout.Diffuse.w);

	float stochasticBlend = (screenNoise * screenNoise) < psout.Diffuse.w ? 1.0 : 0.0;
	psout.NormalGlossiness.w = stochasticBlend;
#	endif

	if ((!inWorld && !inReflection) && SharedData::linearLightingSettings.enableLinearLighting && !(Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow)) {
		psout.Diffuse.xyz = Color::LinearToSrgb(psout.Diffuse.xyz);
	}

	return psout;
}
#endif  // PSHADER
