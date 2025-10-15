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

#define LIGHTING

#if defined(FACEGEN) || defined(FACEGEN_RGB_TINT)
#	define SKIN
#endif

#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

#if (defined(TREE_ANIM) || defined(LANDSCAPE)) && !defined(VC)
#	define VC
#endif  // TREE_ANIM || LANDSCAPE || !VC

#if defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(LODLANDNOISE) || defined(WORLD_MAP)
#	define LOD
#endif

struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 TexCoord0 : TEXCOORD0;
#if !defined(MODELSPACENORMALS)
	float4 Normal : NORMAL0;
	float4 Bitangent : BINORMAL0;
#endif  // !MODELSPACENORMALS

#if defined(VC)
	float4 Color : COLOR0;
#	if defined(LANDSCAPE)
	float4 LandBlendWeights1 : TEXCOORD2;
	float4 LandBlendWeights2 : TEXCOORD3;
#	endif  // LANDSCAPE
#endif      // VC
#if defined(SKINNED)
	float4 BoneWeights : BLENDWEIGHT0;
	float4 BoneIndices : BLENDINDICES0;
#endif  // SKINNED
#if defined(EYE)
	float EyeParameter : TEXCOORD2;
#endif  // EYE
#if defined(VR)
	uint InstanceID : SV_INSTANCEID;
#endif  // VR
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION0;
#if (defined(PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
	float4
#else
	float2
#endif  // (defined (PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
		TexCoord0 : TEXCOORD0;

#if defined(WORLD_MAP)
		float3 InputPosition : TEXCOORD4;
#endif

#if defined(SKINNED) || !defined(MODELSPACENORMALS)
	float3 TBN0 : TEXCOORD1;
	float3 TBN1 : TEXCOORD2;
	float3 TBN2 : TEXCOORD3;
#endif  // defined(SKINNED) || !defined(MODELSPACENORMALS)
#if defined(EYE)
	float3 EyeNormal : TEXCOORD6;
#elif defined(LANDSCAPE)
	float4 LandBlendWeights1 : TEXCOORD6;
	float4 LandBlendWeights2 : TEXCOORD7;
#elif defined(PROJECTED_UV) && !defined(SKINNED)
	float3 TexProj : TEXCOORD7;
#endif  // EYE

	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
	float4 Color : COLOR0;
	float4 FogParam : COLOR1;

#if defined(VR)
	float ClipDistance : SV_ClipDistance0;  // o11
	float CullDistance : SV_CullDistance0;  // p11
#endif

	float3 ModelPosition : TEXCOORD12;
};
#ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
#	if !defined(VR)
	float4 HighDetailRange[1] : packoffset(c0);  // loaded cells center in xy, size in zw
	float4 FogParam : packoffset(c1);
	float4 FogNearColor : packoffset(c2);
	float4 FogFarColor : packoffset(c3);
#	else
	float4 HighDetailRange[2] : packoffset(c0);  // loaded cells center in xy, size in zw
	float4 FogParam : packoffset(c2);
	float4 FogNearColor : packoffset(c3);
	float4 FogFarColor : packoffset(c4);
#	endif  // VR
};

cbuffer PerMaterial : register(b1)
{
	float4 LeftEyeCenter : packoffset(c0);
	float4 RightEyeCenter : packoffset(c1);
	float4 TexcoordOffset : packoffset(c2);
};

cbuffer PerGeometry : register(b2)
{
#	if !defined(VR)
	row_major float3x4 World[1] : packoffset(c0);
	row_major float3x4 PreviousWorld[1] : packoffset(c3);
	float4 EyePosition[1] : packoffset(c6);
	float4 LandBlendParams : packoffset(c7);  // offset in xy, gridPosition in yw
	float4 TreeParams : packoffset(c8);       // wind magnitude in y, amplitude in z, leaf frequency in w
	float2 WindTimers : packoffset(c9);
	row_major float3x4 TextureProj[1] : packoffset(c10);
	float IndexScale : packoffset(c13);
	float4 WorldMapOverlayParameters : packoffset(c14);
#	else   // VR has 49 vs 30 entries
	row_major float3x4 World[2] : packoffset(c0);
	row_major float3x4 PreviousWorld[2] : packoffset(c6);
	float4 EyePosition[2] : packoffset(c12);
	float4 LandBlendParams : packoffset(c14);  // offset in xy, gridPosition in yw
	float4 TreeParams : packoffset(c15);       // wind magnitude in y, amplitude in z, leaf frequency in w
	float2 WindTimers : packoffset(c16);
	row_major float3x4 TextureProj[2] : packoffset(c17);
	float IndexScale : packoffset(c23);
	float4 WorldMapOverlayParameters : packoffset(c24);
#	endif  // VR
};

cbuffer VS_PerFrame : register(b12)
{
#	if !defined(VR)
	row_major float3x3 ScreenProj[1] : packoffset(c0);
	row_major float4x4 ViewProj[1] : packoffset(c8);
#		if defined(SKINNED)
	float3 BonesPivot[1] : packoffset(c40);
	float3 PreviousBonesPivot[1] : packoffset(c41);
#		endif  // SKINNED
#	else
	row_major float3x3 ScreenProj[2] : packoffset(c0);
	row_major float4x4 ViewProj[2] : packoffset(c16);
#		if defined(SKINNED)
	float3 BonesPivot[2] : packoffset(c80);
	float3 PreviousBonesPivot[2] : packoffset(c82);
#		endif  // SKINNED
#	endif      // VR
};

#	if defined(TREE_ANIM)
float2 GetTreeShiftVector(float4 position, float4 color)
{
	precise float4 tmp1 = (TreeParams.w * TreeParams.y).xxxx * WindTimers.xxyy;
	precise float4 tmp2 = float4(0.1, 0.25, 0.1, 0.25) * tmp1 + dot(position.xyz, 1.0.xxx).xxxx;
	precise float4 tmp3 = abs(-1.0.xxxx + 2.0.xxxx * frac(0.5.xxxx + tmp2.xyzw));
	precise float4 tmp4 = (tmp3 * tmp3) * (3.0.xxxx - 2.0.xxxx * tmp3);
	return (tmp4.xz + 0.1.xx * tmp4.yw) * (TreeParams.z * color.w).xx;
}
#	endif  // TREE_ANIM

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	precise float4 inputPosition = float4(input.Position.xyz, 1.0);

	uint eyeIndex = Stereo::GetEyeIndexVS(
#	if defined(VR)
		input.InstanceID
#	endif
	);
#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	inputPosition = LodLandscape::AdjustLodLandscapeVertexPositionMS(inputPosition, float4x4(World[eyeIndex], float4(0, 0, 0, 1)), HighDetailRange[eyeIndex]);
#	endif  // defined(LODLANDNOISE) || defined(LODLANDSCAPE)                                                                   \

	precise float4 previousInputPosition = inputPosition;

#	if defined(TREE_ANIM)
	precise float2 treeShiftVector = GetTreeShiftVector(input.Position, input.Color);
	float3 normal = -1.0.xxx + 2.0.xxx * input.Normal.xyz;

	inputPosition.xyz += normal.xyz * treeShiftVector.x;
	previousInputPosition.xyz += normal.xyz * treeShiftVector.y;
#	endif

#	if defined(SKINNED)
	precise int4 actualIndices = 765.01.xxxx * input.BoneIndices.xyzw;

	float3x4 previousWorldMatrix =
		Skinned::GetBoneTransformMatrix(PreviousBones, actualIndices, PreviousBonesPivot[eyeIndex], input.BoneWeights);
	precise float4 previousWorldPosition =
		float4(mul(inputPosition, transpose(previousWorldMatrix)), 1);

	float3x4 worldMatrix = Skinned::GetBoneTransformMatrix(Bones, actualIndices, BonesPivot[eyeIndex], input.BoneWeights);
	precise float4 worldPosition = float4(mul(inputPosition, transpose(worldMatrix)), 1);

	float4 viewPos = mul(ViewProj[eyeIndex], worldPosition);
#	else   // !SKINNED
	precise float4 previousWorldPosition = float4(mul(PreviousWorld[eyeIndex], inputPosition), 1);
	precise float4 worldPosition = float4(mul(World[eyeIndex], inputPosition), 1);
	precise float4x4 world4x4 = float4x4(World[eyeIndex][0], World[eyeIndex][1], World[eyeIndex][2], float4(0, 0, 0, 1));
	precise float4x4 modelView = mul(ViewProj[eyeIndex], world4x4);
	float4 viewPos = mul(modelView, inputPosition);
#	endif  // SKINNED

	vsout.Position = viewPos;

#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	vsout.Position.z += min(1, 1e-4 * max(0, viewPos.z - 70000)) * 0.5;
#	endif

	float2 uv = input.TexCoord0.xy * TexcoordOffset.zw + TexcoordOffset.xy;
#	if defined(LANDSCAPE)
	vsout.TexCoord0.zw = (uv * 0.010416667.xx + LandBlendParams.xy) * float2(1, -1) + float2(0, 1);
#	elif defined(PROJECTED_UV) && !defined(SKINNED)
	vsout.TexCoord0.z = mul(TextureProj[eyeIndex][0], inputPosition);
	vsout.TexCoord0.w = mul(TextureProj[eyeIndex][1], inputPosition);
#	endif
	vsout.TexCoord0.xy = uv;

#	if defined(WORLD_MAP)
	vsout.InputPosition.xyz = WorldMapOverlayParameters.xyz + worldPosition.xyz;
#	endif

#	if defined(SKINNED)
	float3x3 boneRSMatrix = Skinned::GetBoneRSMatrix(Bones, actualIndices, input.BoneWeights);
#	endif

#	if !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(
		float3(input.Position.w, input.Normal.w * 2 - 1, input.Bitangent.w * 2 - 1),
		input.Bitangent.xyz * 2.0.xxx + -1.0.xxx,
		input.Normal.xyz * 2.0.xxx + -1.0.xxx);
	float3x3 tbnTr = transpose(tbn);

#		if defined(SKINNED)
	float3x3 worldTbnTr = transpose(mul(transpose(tbnTr), transpose(boneRSMatrix)));
	float3x3 worldTbnTrTr = transpose(worldTbnTr);
	worldTbnTrTr[0] = normalize(worldTbnTrTr[0]);
	worldTbnTrTr[1] = normalize(worldTbnTrTr[1]);
	worldTbnTrTr[2] = normalize(worldTbnTrTr[2]);
	worldTbnTr = transpose(worldTbnTrTr);
	vsout.TBN0.xyz = worldTbnTr[0];
	vsout.TBN1.xyz = worldTbnTr[1];
	vsout.TBN2.xyz = worldTbnTr[2];
#		else
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
#		endif
#	elif defined(SKINNED)
	float3x3 boneRSMatrixTr = transpose(boneRSMatrix);
	float3x3 worldTbnTr = transpose(float3x3(normalize(boneRSMatrixTr[0]),
		normalize(boneRSMatrixTr[1]), normalize(boneRSMatrixTr[2])));

	vsout.TBN0.xyz = worldTbnTr[0];
	vsout.TBN1.xyz = worldTbnTr[1];
	vsout.TBN2.xyz = worldTbnTr[2];
#	endif

#	if defined(LANDSCAPE)
	vsout.LandBlendWeights1 = input.LandBlendWeights1;

	float2 gridOffset = LandBlendParams.zw - input.Position.xy;
	vsout.LandBlendWeights2.w = 1 - saturate(0.000375600968 * (9625.59961 - length(gridOffset)));
	vsout.LandBlendWeights2.xyz = input.LandBlendWeights2.xyz;
#	elif defined(PROJECTED_UV) && !defined(SKINNED)
	vsout.TexProj = TextureProj[eyeIndex][2].xyz;
#	endif

#	if defined(EYE)
	precise float4 modelEyeCenter = float4(LeftEyeCenter.xyz + input.EyeParameter.xxx * (RightEyeCenter.xyz - LeftEyeCenter.xyz), 1);
	vsout.EyeNormal.xyz = normalize(worldPosition.xyz - mul(modelEyeCenter, transpose(worldMatrix)));
#	endif  // EYE

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

#	if defined(VR)
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(vsout.Position, eyeIndex);
	vsout.Position = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#	endif  // VR

	vsout.ModelPosition = input.Position.xyz;

	return vsout;
}
#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#if !defined(LANDSCAPE)
#	undef TERRAIN_BLENDING
#endif

#if defined(DEFERRED)
struct PS_OUTPUT
{
	float4 Diffuse : SV_Target0;
	float4 MotionVectors : SV_Target1;
	float4 NormalGlossiness : SV_Target2;
	float4 Albedo : SV_Target3;
	float4 Specular : SV_Target4;
	float4 Reflectance : SV_Target5;
	float4 Masks : SV_Target6;
#	if defined(SNOW)
	float4 Parameters : SV_Target7;
#	endif
};
#else
struct PS_OUTPUT
{
	float4 Diffuse : SV_Target0;
	float4 MotionVectors : SV_Target1;
};
#endif

#ifdef PSHADER

SamplerState SampTerrainParallaxSampler : register(s1);

#	if defined(LANDSCAPE)

SamplerState SampColorSampler : register(s0);

#		define SampLandColor2Sampler SampColorSampler
#		define SampLandColor3Sampler SampColorSampler
#		define SampLandColor4Sampler SampColorSampler
#		define SampLandColor5Sampler SampColorSampler
#		define SampLandColor6Sampler SampColorSampler
#		define SampNormalSampler SampColorSampler
#		define SampLandNormal2Sampler SampColorSampler
#		define SampLandNormal3Sampler SampColorSampler
#		define SampLandNormal4Sampler SampColorSampler
#		define SampLandNormal5Sampler SampColorSampler
#		define SampLandNormal6Sampler SampColorSampler
#		define SampRMAOSSampler SampColorSampler
#		define SampLandRMAOS2Sampler SampColorSampler
#		define SampLandRMAOS3Sampler SampColorSampler
#		define SampLandRMAOS4Sampler SampColorSampler
#		define SampLandRMAOS5Sampler SampColorSampler
#		define SampLandRMAOS6Sampler SampColorSampler

#	else

SamplerState SampColorSampler : register(s0);

#		define SampNormalSampler SampColorSampler

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
SamplerState SampSpecularSampler : register(s2);
#		endif
#		if defined(FACEGEN)
SamplerState SampTintSampler : register(s3);
SamplerState SampDetailSampler : register(s4);
#		elif defined(PARALLAX)
SamplerState SampParallaxSampler : register(s3);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
SamplerState SampProjDiffuseSampler : register(s3);
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(SNOW_FLAG) || defined(EYE)) && !defined(FACEGEN)
SamplerState SampEnvSampler : register(s4);
SamplerState SampEnvMaskSampler : register(s5);
#		endif

#		if defined(TRUE_PBR)
SamplerState SampParallaxSampler : register(s4);
SamplerState SampRMAOSSampler : register(s5);
#		endif

SamplerState SampGlowSampler : register(s6);

#		if defined(MULTI_LAYER_PARALLAX)
SamplerState SampLayerSampler : register(s8);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
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

#	endif

#	if defined(LOD_LAND_BLEND)
SamplerState SampLandLodBlend1Sampler : register(s13);
SamplerState SampLandLodBlend2Sampler : register(s15);
#	elif defined(LODLANDNOISE)
SamplerState SampLandLodNoiseSampler : register(s15);
#	endif

SamplerState SampShadowMaskSampler : register(s14);

#	if defined(LANDSCAPE)

Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexLandColor2Sampler : register(t1);
Texture2D<float4> TexLandColor3Sampler : register(t2);
Texture2D<float4> TexLandColor4Sampler : register(t3);
Texture2D<float4> TexLandColor5Sampler : register(t4);
Texture2D<float4> TexLandColor6Sampler : register(t5);
Texture2D<float4> TexNormalSampler : register(t7);
Texture2D<float4> TexLandNormal2Sampler : register(t8);
Texture2D<float4> TexLandNormal3Sampler : register(t9);
Texture2D<float4> TexLandNormal4Sampler : register(t10);
Texture2D<float4> TexLandNormal5Sampler : register(t11);
Texture2D<float4> TexLandNormal6Sampler : register(t12);

Texture2D<float4> TexLandTHDisp0Sampler : register(t92);
Texture2D<float4> TexLandTHDisp1Sampler : register(t93);
Texture2D<float4> TexLandTHDisp2Sampler : register(t94);
Texture2D<float4> TexLandTHDisp3Sampler : register(t95);
Texture2D<float4> TexLandTHDisp4Sampler : register(t96);
Texture2D<float4> TexLandTHDisp5Sampler : register(t97);

#		if defined(TRUE_PBR)

Texture2D<float4> TexLandDisplacement0Sampler : register(t80);
Texture2D<float4> TexLandDisplacement1Sampler : register(t81);
Texture2D<float4> TexLandDisplacement2Sampler : register(t82);
Texture2D<float4> TexLandDisplacement3Sampler : register(t83);
Texture2D<float4> TexLandDisplacement4Sampler : register(t84);
Texture2D<float4> TexLandDisplacement5Sampler : register(t85);

Texture2D<float4> TexRMAOSSampler : register(t86);
Texture2D<float4> TexLandRMAOS2Sampler : register(t87);
Texture2D<float4> TexLandRMAOS3Sampler : register(t88);
Texture2D<float4> TexLandRMAOS4Sampler : register(t89);
Texture2D<float4> TexLandRMAOS5Sampler : register(t90);
Texture2D<float4> TexLandRMAOS6Sampler : register(t91);

#		endif

#	else

Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexNormalSampler : register(t1);  // normal in xyz, glossiness in w if not modelspacenormal

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
Texture2D<float4> TexSpecularSampler : register(t2);
#		endif
#		if defined(FACEGEN)
Texture2D<float4> TexTintSampler : register(t3);
Texture2D<float4> TexDetailSampler : register(t4);
#		elif defined(PARALLAX)
Texture2D<float4> TexParallaxSampler : register(t3);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
Texture2D<float4> TexProjDiffuseSampler : register(t3);
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(SNOW_FLAG) || defined(EYE)) && !defined(FACEGEN)
TextureCube<float4> TexEnvSampler : register(t4);
Texture2D<float4> TexEnvMaskSampler : register(t5);
#		endif

#		if defined(TRUE_PBR)
Texture2D<float4> TexParallaxSampler : register(t4);
Texture2D<float4> TexRMAOSSampler : register(t5);
#		endif

Texture2D<float4> TexGlowSampler : register(t6);

#		if defined(MULTI_LAYER_PARALLAX)
Texture2D<float4> TexLayerSampler : register(t8);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
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

#	endif

#	if defined(LOD_LAND_BLEND)
Texture2D<float4> TexLandLodBlend1Sampler : register(t13);
Texture2D<float4> TexLandLodBlend2Sampler : register(t15);
#	elif defined(LODLANDNOISE)
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
#	if !(defined(LANDSCAPE) && defined(TRUE_PBR))
	float4 TintColor : packoffset(c1);
	float4 EnvmapData : packoffset(c2);  // fEnvmapScale in x, 1 or 0 in y depending of if has envmask
	float4 ParallaxOccData : packoffset(c3);
	float4 SpecularColor : packoffset(c4);  // Shininess in w, color in xyz
	float4 SparkleParams : packoffset(c5);
	float4 MultiLayerParallaxData : packoffset(c6);  // Layer thickness in x, refraction scale in y, uv scale in zw
#	else
	float4 LandscapeTexture1GlintParameters : packoffset(c1);
	float4 LandscapeTexture2GlintParameters : packoffset(c2);
	float4 LandscapeTexture3GlintParameters : packoffset(c3);
	float4 LandscapeTexture4GlintParameters : packoffset(c4);
	float4 LandscapeTexture5GlintParameters : packoffset(c5);
	float4 LandscapeTexture6GlintParameters : packoffset(c6);
#	endif
	float4 LightingEffectParams : packoffset(c7);  // fSubSurfaceLightRolloff in x, fRimLightPower in y
	float4 IBLParams : packoffset(c8);

#	if !defined(TRUE_PBR)
	float4 LandscapeTexture1to4IsSnow : packoffset(c9);
	float4 LandscapeTexture5to6IsSnow : packoffset(c10);  // bEnableSnowMask in z, inverse iLandscapeMultiNormalTilingFactor in w
	float4 LandscapeTexture1to4IsSpecPower : packoffset(c11);
	float4 LandscapeTexture5to6IsSpecPower : packoffset(c12);
	float4 SnowRimLightParameters : packoffset(c13);  // fSnowRimLightIntensity in x, fSnowGeometrySpecPower in y, fSnowNormalSpecPower in z, bEnableSnowRimLighting in w
#	endif

#	if defined(TRUE_PBR) && defined(LANDSCAPE)
	float3 LandscapeTexture2PBRParams : packoffset(c9);
	float3 LandscapeTexture3PBRParams : packoffset(c10);
	float3 LandscapeTexture4PBRParams : packoffset(c11);
	float3 LandscapeTexture5PBRParams : packoffset(c12);
	float3 LandscapeTexture6PBRParams : packoffset(c13);
#	endif

	float4 CharacterLightParams : packoffset(c14);
	// VR is [9] instead of [15]

	uint PBRFlags : packoffset(c15.x);
	float3 PBRParams1 : packoffset(c15.y);  // roughness scale, displacement scale, specular level
	float4 PBRParams2 : packoffset(c16);    // subsurface color, subsurface opacity
};

cbuffer PerGeometry : register(b2)
{
#	if !defined(VR)
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
#	else
	// VR is [49] instead of [30]
	float3 DirLightDirection : packoffset(c0);
	float4 UnknownPerGeometry[12] : packoffset(c1);
	float3 DirLightColor : packoffset(c13);
	float4 ShadowLightMaskSelect : packoffset(c14);
	float4 MaterialData : packoffset(c15);  // envmapLODFade in x, specularLODFade in y, alpha in z
	float AlphaTestRef : packoffset(c16);
	float3 EmitColor : packoffset(c16.y);
	float4 ProjectedUVParams : packoffset(c18);
	float4 SSRParams : packoffset(c19);
	float4 WorldMapOverlayParametersPS : packoffset(c20);
	float4 ProjectedUVParams2 : packoffset(c21);
	float4 ProjectedUVParams3 : packoffset(c22);  // fProjectedUVDiffuseNormalTilingScale in x,	fProjectedUVNormalDetailTilingScale in y, EnableProjectedNormals in w
	row_major float3x4 DirectionalAmbient : packoffset(c23);
	float4 AmbientSpecularTintAndFresnelPower : packoffset(c26);  // Fresnel power in z, color in xyz
	float4 PointLightPosition[14] : packoffset(c27);              // point light radius in w
	float4 PointLightColor[7] : packoffset(c41);
	float2 NumLightNumShadowLight : packoffset(c48);
#	endif  // VR
};

#	if !defined(VR)
cbuffer AlphaTestRefBuffer : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif

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

float3 GetLightSpecularInput(PS_INPUT input, float3 L, float3 V, float3 N, float3 lightColor, float shininess, float2 uv)
{
	float3 H = normalize(V + L);
	float HdotN = 1.0;
#	if defined(ANISO_LIGHTING)
	float3 AN = normalize(N * 0.5 + float3(input.TBN0.z, input.TBN1.z, input.TBN2.z));
	float LdotAN = dot(AN, L);
	float HdotAN = dot(AN, H);
	HdotN = 1 - min(1, abs(LdotAN - HdotAN));
#	else
	HdotN = saturate(dot(H, N));
#	endif

#	if defined(SPECULAR)
	float lightColorMultiplier = exp2(shininess * log2(HdotN));

#	elif defined(SPARKLE)
	float lightColorMultiplier = 0;
#	else
	float lightColorMultiplier = HdotN;
#	endif

#	if defined(ANISO_LIGHTING)
	lightColorMultiplier *= 0.7 * max(0, L.z);
#	endif

#	if defined(SPARKLE) && !defined(SNOW)
	float3 sparkleUvScale = exp2(float3(1.3, 1.6, 1.9) * log2(abs(SparkleParams.x)).xxx);

	float sparkleColor1 = TexProjDetail.Sample(SampProjDetailSampler, uv * sparkleUvScale.xx).z;
	float sparkleColor2 = TexProjDetail.Sample(SampProjDetailSampler, uv * sparkleUvScale.yy).z;
	float sparkleColor3 = TexProjDetail.Sample(SampProjDetailSampler, uv * sparkleUvScale.zz).z;
	float sparkleColor = ProcessSparkleColor(sparkleColor1) + ProcessSparkleColor(sparkleColor2) + ProcessSparkleColor(sparkleColor3);
	float VdotN = dot(V, N);
	V += N * -(2 * VdotN);
	float sparkleMultiplier = exp2(SparkleParams.w * log2(saturate(dot(V, -L)))) * (SparkleParams.z * sparkleColor);
	sparkleMultiplier = sparkleMultiplier >= 0.5 ? 1 : 0;
	lightColorMultiplier += sparkleMultiplier * HdotN;
#	endif
	return lightColor * lightColorMultiplier;
}

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
#	if defined(SNOW) && !defined(TRUE_PBR)
	if (landSnowMask > 1e-5 && LandscapeTexture5to6IsSnow.w != 1.0) {
		float3 snowNormal =
			float3(-1, -1, 1) *
			TransformNormal(texNormal.Sample(sampNormal, LandscapeTexture5to6IsSnow.ww * uv).xyz);
		landNormal.z += 1;
		float normalProjection = dot(landNormal, snowNormal);
		snowNormal = landNormal * normalProjection.xxx - snowNormal * landNormal.z;
		return normalize(snowNormal);
	} else {
		return landNormal;
	}
#	else
	return landNormal;
#	endif
}

#	if defined(SNOW) && !defined(TRUE_PBR)
float3 GetSnowSpecularColor(PS_INPUT input, float3 worldNormal, float3 viewDirection)
{
	if (SnowRimLightParameters.w > 1e-5) {
#		if defined(MODELSPACENORMALS) && !defined(SKINNED)
		float3 modelGeometryNormal = float3(0, 0, 1);
#		else
		float3 modelGeometryNormal = normalize(float3(input.TBN0.z, input.TBN1.z, input.TBN2.z));
#		endif
		float normalFactor = 1 - saturate(dot(worldNormal, viewDirection));
		float geometryNormalFactor = 1 - saturate(dot(modelGeometryNormal, viewDirection));
		return (SnowRimLightParameters.x * (exp2(SnowRimLightParameters.y * log2(geometryNormalFactor)) * exp2(SnowRimLightParameters.z * log2(normalFactor)))).xxx;
	} else {
		return 0.0.xxx;
	}
}
#	endif

#	if defined(FACEGEN)
float3 GetFacegenBaseColor(float3 rawBaseColor, float2 uv)
{
	float3 detailColor = TexDetailSampler.Sample(SampDetailSampler, uv).xyz;
	detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);
	float3 tintColor = TexTintSampler.Sample(SampTintSampler, uv).xyz;
	tintColor = tintColor * rawBaseColor * 2.0.xxx;
	tintColor = tintColor - tintColor * rawBaseColor;
	return (rawBaseColor * rawBaseColor + tintColor) * detailColor;
}
#	endif

#	if defined(FACEGEN_RGB_TINT)
float3 GetFacegenRGBTintBaseColor(float3 rawBaseColor, float2 uv)
{
	float3 tintColor = TintColor.xyz * rawBaseColor * 2.0.xxx;
	tintColor = tintColor - tintColor * rawBaseColor;
	return float3(1.01171875, 0.99609375, 1.01171875) * (rawBaseColor * rawBaseColor + tintColor);
}
#	endif

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
	bool useLodColorZ = lodColorMul.w > 0.5;
	lodColor.xyz = max(lodColor.xyz, rawBaseColor.xyz);
	lodColor.w = useLodColorZ ? lodColor.z : min(lodColor.w, rawBaseColor.z);
	return (0.5 * lodMultiplier).xxx * (lodColor.xyw - rawBaseColor.xyz) + rawBaseColor;
#		else
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.199999988, 0.441000015, 0.269999981, 0.281000018) + float4(0.300000012, 0.465000004, 0.0780000091, 0.09799999);
	float3 lodColor = lodColorMul.zwy * 2.0.xxx;
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

#	if defined(LOD)
#		undef EXTENDED_MATERIALS
#		undef WATER_BLENDING
#		undef LIGHT_LIMIT_FIX
#		undef WETNESS_EFFECTS
#		undef DYNAMIC_CUBEMAPS
#		undef WATER_EFFECTS
#	endif

#	if defined(WORLD_MAP)
#		undef CLOUD_SHADOWS
#		undef SKYLIGHTING
#	endif

#	if defined(WATER_EFFECTS)
#		include "WaterEffects/WaterCaustics.hlsli"
#	endif

#	if defined(EYE)
#		undef WETNESS_EFFECTS
#	endif

#	if defined(EXTENDED_MATERIALS) && !defined(LOD) && (defined(PARALLAX) || defined(LANDSCAPE) || defined(ENVMAP) || defined(TRUE_PBR))
#		define EMAT
#	endif

#	if defined(EMAT) && (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#		define EMAT_ENVMAP
#	endif

#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(TRUE_PBR)
#		include "Common/PBR.hlsli"
#	endif

#	if defined(EMAT)
#		include "ExtendedMaterials/ExtendedMaterials.hlsli"
#	endif

#	if defined(SCREEN_SPACE_SHADOWS)
#		include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#	endif

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#		include "InverseSquareLighting/InverseSquareLighting.hlsli"
#	endif

#	if defined(TREE_ANIM)
#		undef WETNESS_EFFECTS
#	endif

#	if defined(WETNESS_EFFECTS)
#		include "WetnessEffects/WetnessEffects.hlsli"
#	endif

#	if defined(TERRAIN_BLENDING)
#		include "TerrainBlending/TerrainBlending.hlsli"
#	endif

#	if defined(SSS) && defined(SKIN) && defined(DEFERRED)
#		undef SOFT_LIGHTING
#	endif

#	if defined(TERRAIN_SHADOWS)
#		include "TerrainShadows/TerrainShadows.hlsli"
#	endif

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	if defined(HAIR) && defined(CS_HAIR)
#		include "Hair/Hair.hlsli"
#	endif

#	if defined(TERRAIN_VARIATION)
#		include "TerrainVariation/TerrainVariation.hlsli"
#	endif

#	if defined(EXTENDED_TRANSLUCENCY) && !(defined(LOD) || defined(SKIN) || defined(HAIR) || defined(EYE) || defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS) || defined(DEPTH_WRITE_DECALS))
#		include "ExtendedTranslucency/ExtendedTranslucency.hlsli"
#		define ANISOTROPIC_ALPHA
#	endif

#	define LinearSampler SampColorSampler

#	include "Common/ShadowSampling.hlsli"

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

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

	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, viewPosition.z);

#	if defined(SKINNED) || !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(input.TBN0.xyz, input.TBN1.xyz, input.TBN2.xyz);

#		if !defined(TREE_ANIM) && !defined(LOD)
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
#		if defined(LANDSCAPE)
	float shininess = dot(input.LandBlendWeights1, LandscapeTexture1to4IsSpecPower) + input.LandBlendWeights2.x * LandscapeTexture5to6IsSpecPower.x + input.LandBlendWeights2.y * LandscapeTexture5to6IsSpecPower.y;
#		else
	float shininess = SpecularColor.w;
#		endif  // defined (LANDSCAPE)
#	endif

#	if defined(TERRAIN_BLENDING)
	float depthSampled = TerrainBlending::TerrainBlendingMaskTexture[input.Position.xy].x;

	float depthSampledLinear = SharedData::GetScreenDepth(depthSampled);
	float depthPixelLinear = SharedData::GetScreenDepth(input.Position.z);

	float blendFactorTerrain = saturate((depthSampledLinear - depthPixelLinear) / 10.0);

	if (input.Position.z == depthSampled)
		blendFactorTerrain = 1;

	blendFactorTerrain = saturate(blendFactorTerrain);
#	endif

	float2 uv = input.TexCoord0.xy;
	float2 uvOriginal = uv;

#	if defined(EMAT)
	float parallaxShadowQuality = sqrt(1.0 - saturate(viewPosition.z / 2048.0));
#	endif

#	if defined(LANDSCAPE)
	float mipLevels[6];
#	else
	float mipLevel = 0;
#	endif  // LANDSCAPE
	float sh0 = 0;
	float pixelOffset = 0;

#	if defined(EMAT)
#		if defined(LANDSCAPE)
	DisplacementParams displacementParams[6];
	displacementParams[0].DisplacementScale = 1.f;
	displacementParams[0].DisplacementOffset = 0.f;
	displacementParams[0].HeightScale = 1;
	displacementParams[0].FlattenAmount = 0;
#		else
	DisplacementParams displacementParams;
	displacementParams.DisplacementScale = 1.f;
	displacementParams.DisplacementOffset = 0.f;
	displacementParams.HeightScale = 1;
	displacementParams.FlattenAmount = 0;
#		endif

#	endif

	float curvature = 0;
	float normalSmoothness = 0;

#	if !defined(MODELSPACENORMALS)
	float3 vertexNormal = tbnTr[2];
#		if defined(EMAT)

	if (SharedData::extendedMaterialSettings.EnableParallaxWarpingFix) {
		float3 ndx = ddx(vertexNormal);
		float3 ndy = ddy(vertexNormal);
		float3 fdx = ddx(input.WorldPosition.xyz);
		float3 fdy = ddy(input.WorldPosition.xyz);
		float fragSize = rcp(length(max(abs(fdx), abs(fdy))));
		curvature = pow(length(max(abs(ndx), abs(ndy))) * fragSize, 0.5);
		float3 flatWorldNormal = normalize(-cross(ddx(input.WorldPosition.xyz), ddy(input.WorldPosition.xyz)));
		normalSmoothness = (1 - dot(vertexNormal, flatWorldNormal));
#			if defined(LANDSCAPE)
		displacementParams[0].HeightScale = saturate(1 - curvature);
		displacementParams[0].FlattenAmount = (normalSmoothness + curvature);
#			else
		displacementParams.HeightScale = saturate(1 - curvature);
		displacementParams.FlattenAmount = (normalSmoothness + curvature);
#			endif
	}
#		endif
#	endif

	float3 entryNormal = 0;
	float3 entryNormalTS = 0;
	float eta = 1;
	float3 refractedViewDirection = viewDirection;
	float4 sampledCoatColor = PBRParams2;
	float3 complexSpecular = 1.0;  // Declare complexSpecular at a higher scope so it's available throughout the shader (NEEDED FOR STOCH. FIX)
#	if defined(EMAT)
#		if defined(PARALLAX)
	if (SharedData::extendedMaterialSettings.EnableParallax) {
		mipLevel = ExtendedMaterials::GetMipLevel(uv, TexParallaxSampler);
		uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, TexParallaxSampler, SampParallaxSampler, 0, displacementParams, pixelOffset);
		if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows))
			sh0 = TexParallaxSampler.SampleLevel(SampParallaxSampler, uv, mipLevel).x;
	}
#		endif  // PARALLAX

	bool complexMaterial = false;
	bool complexMaterialParallax = false;
	float4 complexMaterialColor = 1.0;

#		if defined(EMAT_ENVMAP)

	if (SharedData::extendedMaterialSettings.EnableComplexMaterial) {
		float envMaskTest = TexEnvMaskSampler.SampleLevel(SampEnvMaskSampler, uv, 15).w;
		complexMaterial = envMaskTest < (1.0 - (4.0 / 255.0));

		if (complexMaterial) {
			if (envMaskTest > (4.0 / 255.0)) {
				complexMaterialParallax = true;
				mipLevel = ExtendedMaterials::GetMipLevel(uv, TexEnvMaskSampler);
				uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, TexEnvMaskSampler, SampTerrainParallaxSampler, 3, displacementParams, pixelOffset);
				if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows))
					sh0 = TexEnvMaskSampler.SampleLevel(SampEnvMaskSampler, uv, mipLevel).w;
			}

			complexMaterialColor = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv);
		}
	}

#		endif  // ENVMAP

#		if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	bool PBRParallax = false;
	[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
	{
		float4 sampledCoatProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
		sampledCoatColor.rgb *= Color::Diffuse(sampledCoatProperties.rgb);
		sampledCoatColor.a *= sampledCoatProperties.a;
	}
	[branch] if (SharedData::extendedMaterialSettings.EnableParallax && (PBRFlags & PBR::Flags::HasDisplacement) != 0)
	{
		PBRParallax = true;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			displacementParams.HeightScale = PBRParams1.y;
			displacementParams.DisplacementScale = 0.5;
			displacementParams.DisplacementOffset = -0.25;

			eta = lerp(1.0, (1 - sqrt(MultiLayerParallaxData.y)) / (1 + sqrt(MultiLayerParallaxData.y)), sampledCoatColor.w);
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				entryNormalTS = normalize(TransformNormal(TexBackLightSampler.Sample(SampBackLightSampler, uvOriginal).xyz));
			}
			else
			{
				entryNormalTS = normalize(TransformNormal(TexNormalSampler.Sample(SampNormalSampler, uvOriginal).xyz));
			}
			entryNormal = normalize(mul(tbn, entryNormalTS));
			refractedViewDirection = -refract(-viewDirection, entryNormal, eta);
		}
		else
		{
			displacementParams.HeightScale *= PBRParams1.y;
		}
		mipLevel = ExtendedMaterials::GetMipLevel(uv, TexParallaxSampler);
		uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, refractedViewDirection, tbnTr, screenNoise, TexParallaxSampler, SampParallaxSampler, 0, displacementParams, pixelOffset);
		if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows))
			sh0 = TexParallaxSampler.SampleLevel(SampParallaxSampler, uv, mipLevel).x;
	}
#		endif  // TRUE_PBR

#	endif  // EMAT

#	if defined(SNOW)
	bool useSnowSpecular = true;
#	else
	bool useSnowSpecular = false;
#	endif  // SNOW

#	if defined(SPARKLE) || !defined(PROJECTED_UV)
	bool useSnowDecalSpecular = true;
#	else
	bool useSnowDecalSpecular = false;
#	endif  // defined(SPARKLE) || !defined(PROJECTED_UV)

	float2 diffuseUv = uv;

#	if defined(SPARKLE)
	diffuseUv = ProjectedUVParams2.yy * input.TexCoord0.zw;
#	endif  // SPARKLE

#	if defined(LANDSCAPE)
	// Normalise blend weights
	float totalWeight = input.LandBlendWeights1.x + input.LandBlendWeights1.y + input.LandBlendWeights1.z +
	                    input.LandBlendWeights1.w + input.LandBlendWeights2.x + input.LandBlendWeights2.y;
	if (totalWeight > 0.0) {
		input.LandBlendWeights1 /= totalWeight;
		input.LandBlendWeights2.xy /= totalWeight;
	}
	float3 blendedRGB = 0;
	float blendedAlpha = 0;
	float3 blendedNormalRGB = 0;
	float blendedNormalAlpha = 0;

#		if defined(TRUE_PBR)
	float4 blendedRMAOS = 0;
#		endif

	// Compute stochastic offsets and derivatives once for all layers (only when terrain variation is enabled)
#		if defined(TERRAIN_VARIATION)
			bool useTerrainVariation = SharedData::terrainVariationSettings.enableTilingFix;
			// Initialise dx, dy, and sharedOffset for when Terrain Variation is disabled via enableTilingFix but still #defined
			float2 dx = 0, dy = 0;
			StochasticOffsets sharedOffset;
			sharedOffset.offset1 = float2(0, 0);
			sharedOffset.offset2 = float2(0, 0);
			sharedOffset.offset3 = float2(0, 0);
			sharedOffset.weights = float3(0, 0, 0);
			[branch] if (useTerrainVariation)
			{
				dx = ddx(input.TexCoord0.zw);
				dy = ddy(input.TexCoord0.zw);
				sharedOffset = ComputeStochasticOffsets(input.TexCoord0.zw);
			}
#		endif

#		if defined(EMAT)
#			if defined(TRUE_PBR)
	if (SharedData::extendedMaterialSettings.EnableParallax) {
#			else
	if (SharedData::extendedMaterialSettings.EnableTerrainParallax || (SharedData::extendedMaterialSettings.EnableParallax && Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement)) {
#			endif
		mipLevels[0] = ExtendedMaterials::GetMipLevel(uv, TexColorSampler);
		mipLevels[1] = ExtendedMaterials::GetMipLevel(uv, TexLandColor2Sampler);
		mipLevels[2] = ExtendedMaterials::GetMipLevel(uv, TexLandColor3Sampler);
		mipLevels[3] = ExtendedMaterials::GetMipLevel(uv, TexLandColor4Sampler);
		mipLevels[4] = ExtendedMaterials::GetMipLevel(uv, TexLandColor5Sampler);
		mipLevels[5] = ExtendedMaterials::GetMipLevel(uv, TexLandColor6Sampler);

		displacementParams[1] = displacementParams[0];
		displacementParams[2] = displacementParams[0];
		displacementParams[3] = displacementParams[0];
		displacementParams[4] = displacementParams[0];
		displacementParams[5] = displacementParams[0];
#			if defined(TRUE_PBR)
		displacementParams[0].HeightScale *= PBRParams1.y;
		displacementParams[1].HeightScale *= LandscapeTexture2PBRParams.y;
		displacementParams[2].HeightScale *= LandscapeTexture3PBRParams.y;
		displacementParams[3].HeightScale *= LandscapeTexture4PBRParams.y;
		displacementParams[4].HeightScale *= LandscapeTexture5PBRParams.y;
		displacementParams[5].HeightScale *= LandscapeTexture6PBRParams.y;
#			endif

		float weights[6];
		// Initialize weights array
		weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0.0;
#			if defined(TERRAIN_VARIATION)
		uv = ExtendedMaterials::GetParallaxCoords(input, viewPosition.z, uv, mipLevels, viewDirection, tbnTr, screenNoise, displacementParams, sharedOffset, dx, dy, pixelOffset, weights);
#			else
		uv = ExtendedMaterials::GetParallaxCoords(input, viewPosition.z, uv, mipLevels, viewDirection, tbnTr, screenNoise, displacementParams, pixelOffset, weights);
#			endif
		if (SharedData::extendedMaterialSettings.EnableHeightBlending) {
			input.LandBlendWeights1.x = weights[0];
			input.LandBlendWeights1.y = weights[1];
			input.LandBlendWeights1.z = weights[2];
			input.LandBlendWeights1.w = weights[3];
			input.LandBlendWeights2.x = weights[4];
			input.LandBlendWeights2.y = weights[5];
		}
		if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows)) {
#			if defined(TERRAIN_VARIATION)
			sh0 = ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, parallaxShadowQuality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, weights);
			float shadowMultiplier = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, DirLightDirection, sh0, parallaxShadowQuality, screenNoise, displacementParams, sharedOffset, dx, dy);
#			else
			sh0 = ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, parallaxShadowQuality, input.LandBlendWeights1, input.LandBlendWeights2.xy, weights);
#			endif
		}
	}

#		if defined(TERRAIN_VARIATION)
	else if (useTerrainVariation) {
		// Calculate proper mip levels for terrain variation when parallax is disabled but EMAT is available
		mipLevels[0] = ExtendedMaterials::GetMipLevel(uv, TexColorSampler);
		mipLevels[1] = ExtendedMaterials::GetMipLevel(uv, TexLandColor2Sampler);
		mipLevels[2] = ExtendedMaterials::GetMipLevel(uv, TexLandColor3Sampler);
		mipLevels[3] = ExtendedMaterials::GetMipLevel(uv, TexLandColor4Sampler);
		mipLevels[4] = ExtendedMaterials::GetMipLevel(uv, TexLandColor5Sampler);
		mipLevels[5] = ExtendedMaterials::GetMipLevel(uv, TexLandColor6Sampler);
	}
#		endif
#		else
	// Initialize mip levels for non-EMAT case
	mipLevels[0] = mipLevels[1] = mipLevels[2] = mipLevels[3] = mipLevels[4] = mipLevels[5] = 0.0;
#		endif  // EMAT
#	endif      // LANDSCAPE

#	if defined(SPARKLE)
	diffuseUv = ProjectedUVParams2.yy * (input.TexCoord0.zw + (uv - uvOriginal));
#	else
	diffuseUv = uv;
#	endif  // SPARKLE

	float4 baseColor = 0;
	float4 normal = 0;
	float glossiness = 0;

	float4 rawRMAOS = 0;

	float4 glintParameters = 0;

#	if defined(SNOW)  // Earlier snow definition for Terrain Variation rework.
#		if !defined(TRUE_PBR)
	float landSnowMask = 0.0;
#			if defined(LANDSCAPE)
	landSnowMask = GetLandSnowMaskValue(baseColor.w);
#			endif
#		endif
#	endif

#	if defined(LANDSCAPE)
	// Layer 1 (LandBlendWeights1.x)
	if (input.LandBlendWeights1.x > 0.01) {
		float weight = input.LandBlendWeights1.x;

		// Sample diffuse texture for layer 1
#		if defined(TERRAIN_VARIATION)
		float4 landColor1;
		[branch] if (useTerrainVariation)
		{
			landColor1 = StochasticEffect(TexColorSampler, SampColorSampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor1 = TexColorSampler.SampleBias(SampColorSampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor1 = TexColorSampler.SampleBias(SampColorSampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB1 = landColor1.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile0PBR) == 0)
		{
			landColorRGB1 = landColorRGB1 / Color::PBRLightingScale;
		}
#		endif
		float landAlpha1 = landColor1.a;
		float landSnowMask1 = GetLandSnowMaskValue(landColor1.w);

		// Sample normal texture for layer 1
#		if defined(TERRAIN_VARIATION)
		float4 landNormal1;
		[branch] if (useTerrainVariation)
		{
			landNormal1 = StochasticEffect(TexNormalSampler, SampNormalSampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal1 = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal1 = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB1 = landNormal1.rgb;
		float landNormalAlpha1 = landNormal1.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.x * input.LandBlendWeights1.x * landSnowMask1;
#		endif  // SNOW

		// Sample RMAOS texture for layer 1
#		if defined(TRUE_PBR)
		float4 landRMAOS1;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile0PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS1 = StochasticEffect(TexRMAOSSampler, SampRMAOSSampler, uv, sharedOffset, dx, dy) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
			}
			else
			{
				landRMAOS1 = TexRMAOSSampler.SampleBias(SampRMAOSSampler, uv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
			}
#			else
			landRMAOS1 = TexRMAOSSampler.SampleBias(SampRMAOSSampler, uv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile0HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture1GlintParameters;
			}
		}
		else
		{
			landRMAOS1 = input.LandBlendWeights1.x * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS1 * weight;
#		endif
		blendedRGB += landColorRGB1 * weight;
		blendedAlpha += landAlpha1 * weight;
		blendedNormalRGB += landNormalRGB1 * weight;
		blendedNormalAlpha += landNormalAlpha1 * weight;
	}

	// Layer 2 (LandBlendWeights1.y)
	if (input.LandBlendWeights1.y > 0.01) {
		float weight = input.LandBlendWeights1.y;

		// Sample diffuse texture for layer 2
#		if defined(TERRAIN_VARIATION)
		float4 landColor2;
		[branch] if (useTerrainVariation)
		{
			landColor2 = StochasticEffect(TexLandColor2Sampler, SampLandColor2Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor2 = TexLandColor2Sampler.SampleBias(SampLandColor2Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor2 = TexLandColor2Sampler.SampleBias(SampLandColor2Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB2 = landColor2.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1PBR) == 0)
		{
			landColorRGB2 = landColorRGB2 / Color::PBRLightingScale;
		}
#		endif
		float landAlpha2 = landColor2.a;
		float landSnowMask2 = GetLandSnowMaskValue(landColor2.w);

		// Sample normal texture for layer 2
#		if defined(TERRAIN_VARIATION)
		float4 landNormal2;
		[branch] if (useTerrainVariation)
		{
			landNormal2 = StochasticEffect(TexLandNormal2Sampler, SampLandNormal2Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal2 = TexLandNormal2Sampler.SampleBias(SampLandNormal2Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal2 = TexLandNormal2Sampler.SampleBias(SampLandNormal2Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB2 = landNormal2.rgb;
		float landNormalAlpha2 = landNormal2.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.y * input.LandBlendWeights1.y * landSnowMask2;
#		endif  // SNOW

		// Sample RMAOS texture for layer 2
#		if defined(TRUE_PBR)
		float4 landRMAOS2;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS2 = StochasticEffect(TexLandRMAOS2Sampler, SampLandRMAOS2Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture2PBRParams.x, 1, 1, LandscapeTexture2PBRParams.z);
			}
			else
			{
				landRMAOS2 = TexLandRMAOS2Sampler.SampleBias(SampLandRMAOS2Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture2PBRParams.x, 1, 1, LandscapeTexture2PBRParams.z);
			}
#			else
			landRMAOS2 = TexLandRMAOS2Sampler.SampleBias(SampLandRMAOS2Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture2PBRParams.x, 1, 1, LandscapeTexture2PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile1HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture2GlintParameters;
			}
		}
		else
		{
			landRMAOS2 = input.LandBlendWeights1.y * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS2 * weight;
#		endif
		blendedRGB += landColorRGB2 * weight;
		blendedAlpha += landAlpha2 * weight;
		blendedNormalRGB += landNormalRGB2 * weight;
		blendedNormalAlpha += landNormalAlpha2 * weight;
	}

	// Layer 3 (LandBlendWeights1.z)
	if (input.LandBlendWeights1.z > 0.01) {
		float weight = input.LandBlendWeights1.z;
		// Sample diffuse texture for layer 3
#		if defined(TERRAIN_VARIATION)
		float4 landColor3;
		[branch] if (useTerrainVariation)
		{
			landColor3 = StochasticEffect(TexLandColor3Sampler, SampLandColor3Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor3 = TexLandColor3Sampler.SampleBias(SampLandColor3Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor3 = TexLandColor3Sampler.SampleBias(SampLandColor3Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB3 = landColor3.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2PBR) == 0)
		{
			landColorRGB3 = landColorRGB3 / Color::PBRLightingScale;
		}
#		endif
		float landAlpha3 = landColor3.a;
		float landSnowMask3 = GetLandSnowMaskValue(landColor3.w);

		// Sample normal texture for layer 3
#		if defined(TERRAIN_VARIATION)
		float4 landNormal3;
		[branch] if (useTerrainVariation)
		{
			landNormal3 = StochasticEffect(TexLandNormal3Sampler, SampLandNormal3Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal3 = TexLandNormal3Sampler.SampleBias(SampLandNormal3Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal3 = TexLandNormal3Sampler.SampleBias(SampLandNormal3Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB3 = landNormal3.rgb;
		float landNormalAlpha3 = landNormal3.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.z * input.LandBlendWeights1.z * landSnowMask3;
#		endif  // SNOW

		// Sample RMAOS texture for layer 3
#		if defined(TRUE_PBR)
		float4 landRMAOS3;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS3 = StochasticEffect(TexLandRMAOS3Sampler, SampLandRMAOS3Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture3PBRParams.x, 1, 1, LandscapeTexture3PBRParams.z);
			}
			else
			{
				landRMAOS3 = TexLandRMAOS3Sampler.SampleBias(SampLandRMAOS3Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture3PBRParams.x, 1, 1, LandscapeTexture3PBRParams.z);
			}
#			else
			landRMAOS3 = TexLandRMAOS3Sampler.SampleBias(SampLandRMAOS3Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture3PBRParams.x, 1, 1, LandscapeTexture3PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile2HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture3GlintParameters;
			}
		}
		else
		{
			landRMAOS3 = input.LandBlendWeights1.z * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS3 * weight;
#		endif
		blendedRGB += landColorRGB3 * weight;
		blendedAlpha += landAlpha3 * weight;
		blendedNormalRGB += landNormalRGB3 * weight;
		blendedNormalAlpha += landNormalAlpha3 * weight;
	}
	// Layer 4 (LandBlendWeights1.w)
	if (input.LandBlendWeights1.w > 0.01) {
		float weight = input.LandBlendWeights1.w;

		// Sample diffuse texture for layer 4
#		if defined(TERRAIN_VARIATION)
		float4 landColor4;
		[branch] if (useTerrainVariation)
		{
			landColor4 = StochasticEffect(TexLandColor4Sampler, SampLandColor4Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor4 = TexLandColor4Sampler.SampleBias(SampLandColor4Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor4 = TexLandColor4Sampler.SampleBias(SampLandColor4Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB4 = landColor4.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3PBR) == 0)
		{
			landColorRGB4 = landColorRGB4 / Color::PBRLightingScale;
		}
#		endif
		float landAlpha4 = landColor4.a;
		float landSnowMask4 = GetLandSnowMaskValue(landColor4.w);

		// Sample normal texture for layer 4
#		if defined(TERRAIN_VARIATION)
		float4 landNormal4;
		[branch] if (useTerrainVariation)
		{
			landNormal4 = StochasticEffect(TexLandNormal4Sampler, SampLandNormal4Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal4 = TexLandNormal4Sampler.SampleBias(SampLandNormal4Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal4 = TexLandNormal4Sampler.SampleBias(SampLandNormal4Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB4 = landNormal4.rgb;
		float landNormalAlpha4 = landNormal4.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.w * input.LandBlendWeights1.w * landSnowMask4;
#		endif  // SNOW

		// Sample RMAOS texture for layer 4
#		if defined(TRUE_PBR)
		float4 landRMAOS4;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS4 = StochasticEffect(TexLandRMAOS4Sampler, SampLandRMAOS4Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture4PBRParams.x, 1, 1, LandscapeTexture4PBRParams.z);
			}
			else
			{
				landRMAOS4 = TexLandRMAOS4Sampler.SampleBias(SampLandRMAOS4Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture4PBRParams.x, 1, 1, LandscapeTexture4PBRParams.z);
			}
#			else
			landRMAOS4 = TexLandRMAOS4Sampler.SampleBias(SampLandRMAOS4Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture4PBRParams.x, 1, 1, LandscapeTexture4PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile3HasGlint) != 0) {\
				glintParameters += weight * LandscapeTexture4GlintParameters;
			}
		}
		else
		{
			landRMAOS4 = input.LandBlendWeights1.w * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS4 * weight;
#		endif
		blendedRGB += landColorRGB4 * weight;
		blendedAlpha += landAlpha4 * weight;
		blendedNormalRGB += landNormalRGB4 * weight;
		blendedNormalAlpha += landNormalAlpha4 * weight;
	}

	// Layer 5 (LandBlendWeights2.x)
	if (input.LandBlendWeights2.x > 0.01) {
		float weight = input.LandBlendWeights2.x;
		// Sample diffuse texture for layer 5
#		if defined(TERRAIN_VARIATION)
		float4 landColor5;
		[branch] if (useTerrainVariation)
		{
			landColor5 = StochasticEffect(TexLandColor5Sampler, SampLandColor5Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor5 = TexLandColor5Sampler.SampleBias(SampLandColor5Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor5 = TexLandColor5Sampler.SampleBias(SampLandColor5Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB5 = landColor5.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4PBR) == 0)
		{
			landColorRGB5 = landColorRGB5 / Color::PBRLightingScale;
		}
#		endif
		float landAlpha5 = landColor5.a;
		float landSnowMask5 = GetLandSnowMaskValue(landColor5.w);

		// Sample normal texture for layer 5
#		if defined(TERRAIN_VARIATION)
		float4 landNormal5;
		[branch] if (useTerrainVariation)
		{
			landNormal5 = StochasticEffect(TexLandNormal5Sampler, SampLandNormal5Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal5 = TexLandNormal5Sampler.SampleBias(SampLandNormal5Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal5 = TexLandNormal5Sampler.SampleBias(SampLandNormal5Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB5 = landNormal5.rgb;
		float landNormalAlpha5 = landNormal5.a;

#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture5to6IsSnow.x * input.LandBlendWeights2.x * landSnowMask5;
#		endif  // SNOW

		// Sample RMAOS texture for layer 5
#		if defined(TRUE_PBR)
		float4 landRMAOS5;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS5 = StochasticEffect(TexLandRMAOS5Sampler, SampLandRMAOS5Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture5PBRParams.x, 1, 1, LandscapeTexture5PBRParams.z);
			}
			else
			{
				landRMAOS5 = TexLandRMAOS5Sampler.SampleBias(SampLandRMAOS5Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture5PBRParams.x, 1, 1, LandscapeTexture5PBRParams.z);
			}
#			else
			landRMAOS5 = TexLandRMAOS5Sampler.SampleBias(SampLandRMAOS5Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture5PBRParams.x, 1, 1, LandscapeTexture5PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile4HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture5GlintParameters;
			}
		}
		else
		{
			landRMAOS5 = input.LandBlendWeights2.x * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS5 * weight;
#		endif
		blendedRGB += landColorRGB5 * weight;
		blendedAlpha += landAlpha5 * weight;
		blendedNormalRGB += landNormalRGB5 * weight;
		blendedNormalAlpha += landNormalAlpha5 * weight;
	}
	// Layer 6 (LandBlendWeights2.y)
	if (input.LandBlendWeights2.y > 0.01) {
		float weight = input.LandBlendWeights2.y;

		// Sample layer 6 textures
#		if defined(TERRAIN_VARIATION)
		float4 landColor6;
		[branch] if (useTerrainVariation)
		{
			landColor6 = StochasticEffect(TexLandColor6Sampler, SampLandColor6Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor6 = TexLandColor6Sampler.SampleBias(SampLandColor6Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor6 = TexLandColor6Sampler.SampleBias(SampLandColor6Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB6 = landColor6.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5PBR) == 0)
		{
			landColorRGB6 = landColorRGB6 / Color::PBRLightingScale;
		}
#		endif
		float landAlpha6 = landColor6.a;
		float landSnowMask6 = GetLandSnowMaskValue(landColor6.w);

		// Sample normal texture for layer 6
#		if defined(TERRAIN_VARIATION)
		float4 landNormal6;
		[branch] if (useTerrainVariation)
		{
			landNormal6 = StochasticEffect(TexLandNormal6Sampler, SampLandNormal6Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal6 = TexLandNormal6Sampler.SampleBias(SampLandNormal6Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal6 = TexLandNormal6Sampler.SampleBias(SampLandNormal6Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB6 = landNormal6.rgb;
		float landNormalAlpha6 = landNormal6.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture5to6IsSnow.y * 	input.LandBlendWeights2.y * landSnowMask6;
#		endif  // SNOW

		// Sample RMAOS texture for layer 6
#		if defined(TRUE_PBR)
		float4 landRMAOS6;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS6 = StochasticEffect(TexLandRMAOS6Sampler, SampLandRMAOS6Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture6PBRParams.x, 1, 1, LandscapeTexture6PBRParams.z);
			}
			else
			{
				landRMAOS6 = TexLandRMAOS6Sampler.SampleBias(SampLandRMAOS6Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture6PBRParams.x, 1, 1, LandscapeTexture6PBRParams.z);
			}
#			else
			landRMAOS6 = TexLandRMAOS6Sampler.SampleBias(SampLandRMAOS6Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture6PBRParams.x, 1, 1, LandscapeTexture6PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile5HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture6GlintParameters;
			}
		}
		else
		{
			landRMAOS6 = input.LandBlendWeights2.y * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS6 * weight;
#		endif
		blendedRGB += landColorRGB6 * weight;
		blendedAlpha += landAlpha6 * weight;
		blendedNormalRGB += landNormalRGB6 * weight;
		blendedNormalAlpha += landNormalAlpha6 * weight;
	}

	float4 rawBaseColor = float4(blendedRGB, blendedAlpha);
	baseColor = float4(Color::Diffuse(blendedRGB), blendedAlpha);
	normal = float4(blendedNormalRGB, blendedNormalAlpha);
#		if defined(TRUE_PBR)
	rawRMAOS = blendedRMAOS;
#		endif
#	else  // Non-landscape code
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
#	endif


#	if defined(LOD_BLENDING)
#		if defined(LODOBJECTS) || defined(LODOBJECTSHD)
	baseColor.xyz *= SharedData::lodBlendingSettings.LODObjectBrightness;
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
	baseColor.xyz *= SharedData::lodBlendingSettings.LODTerrainBrightness;
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
#	elif (defined(SNOW) && defined(LANDSCAPE))
	normal.xyz = GetLandNormal(landSnowMask1, normal.xyz, uv, SampNormalSampler, TexNormalSampler);
	glossiness = normal.w;
#	else
	normal.xyz = TransformNormal(normal.xyz);
	glossiness = normal.w;
#	endif  // MODELSPACENORMALS

#	if defined(WORLD_MAP)
	normal.xyz = GetWorldMapNormal(input, normal.xyz, rawBaseColor.xyz);
#	endif  // WORLD_MAP

#	if defined(LANDSCAPE)
#		if defined(SNOW) && !defined(TRUE_PBR)
	landSnowMask = LandscapeTexture1to4IsSnow.x * input.LandBlendWeights1.x;
#		endif  // SNOW
#	endif  // LANDSCAPE

#	if defined(EMAT_ENVMAP)
	complexMaterial = complexMaterial && complexMaterialColor.y > (4.0 / 255.0);
	shininess = lerp(shininess, shininess * complexMaterialColor.y, complexMaterial);
	if (complexMaterial) {
		complexSpecular = lerp(1.0, baseColor.xyz, complexMaterialColor.z);
		baseColor.xyz = lerp(baseColor.xyz, 0.0, complexMaterialColor.z);

	}
#	endif  // defined (EMAT) && defined(ENVMAP)

#	if defined(FACEGEN)
	baseColor.xyz = GetFacegenBaseColor(baseColor.xyz, uv);
#	elif defined(FACEGEN_RGB_TINT)
	baseColor.xyz = GetFacegenRGBTintBaseColor(baseColor.xyz, uv);
#	endif  // FACEGEN

#	if defined(HAIR) && defined(CS_HAIR)
	float3 hairTint = 0;

	if (SharedData::hairSpecularSettings.Enabled) {
		hairTint = lerp(1, TintColor.xyz, input.Color.y);
		baseColor.xyz *= hairTint;
		baseColor.xyz = Hair::Saturation(baseColor.xyz, SharedData::hairSpecularSettings.HairSaturation);
		baseColor.xyz *= SharedData::hairSpecularSettings.BaseColorMult;
		baseColor.xyz = SharedData::hairSpecularSettings.HairMode == 1 ? baseColor.xyz * baseColor.xyz : baseColor.xyz;  // To match color for Marschner
	}

	float3 sampledHairFlow = 0;
	bool useHairFlowMap = false;
#		if defined(BACK_LIGHTING)
	if (SharedData::hairSpecularSettings.Enabled) {
		uint2 hairFlowDimensions = uint2(0, 0);
		sampledHairFlow = float3(TexBackLightSampler.Sample(SampBackLightSampler, uv).xy, 0.5f);
		TexBackLightSampler.GetDimensions(hairFlowDimensions.x, hairFlowDimensions.y);
		useHairFlowMap = (sampledHairFlow.x > 0.0 || sampledHairFlow.y > 0.0) && hairFlowDimensions.x > 32 && hairFlowDimensions.y > 32;
		sampledHairFlow = useHairFlowMap ? sampledHairFlow * 2.0f - 1.0f : float3(0.5f, 0.5f, 0.5f);
	}
#		endif
#	endif

#	if defined(LOD_LAND_BLEND)
	float4 lodLandColor;

#		if defined(TERRAIN_VARIATION)
	if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix) {
		// Apply stochastic sampling to LOD_LAND_BLEND color texture
		float2 blendColorUV = input.TexCoord0.zw;
		float2 dx = ddx(blendColorUV);
		float2 dy = ddy(blendColorUV);
		StochasticOffsets lodBlendColorOffset = ComputeStochasticOffsetsLOD(blendColorUV);
		lodLandColor = StochasticSampleLOD(screenNoise, TexLandLodBlend1Sampler, SampLandLodBlend1Sampler, blendColorUV, lodBlendColorOffset, dx, dy);
	} else {
		lodLandColor = TexLandLodBlend1Sampler.Sample(SampLandLodBlend1Sampler, input.TexCoord0.zw);
	}
#		else
	lodLandColor = TexLandLodBlend1Sampler.Sample(SampLandLodBlend1Sampler, input.TexCoord0.zw);
#		endif

#		if defined(LOD_BLENDING)
	lodLandColor.xyz *= SharedData::lodBlendingSettings.LODTerrainBrightness;
#		endif  // LOD_BLENDING
	float lodBlendParameter = GetLodLandBlendParameter(lodLandColor.xyz);
	float lodBlendMask = TexLandLodBlend2Sampler.Sample(SampLandLodBlend2Sampler, 3.0.xx * input.TexCoord0.zw).x;
	float lodLandFadeFactor = GetLodLandBlendMultiplier(lodBlendParameter, lodBlendMask);
	float lodLandBlendFactor = LODTexParams.z * input.LandBlendWeights2.w;
	normal.xyz = lerp(normal.xyz, float3(0, 0, 1), lodLandBlendFactor);

#		if !defined(TRUE_PBR)
	baseColor.w = 0;
	baseColor = lerp(baseColor, lodLandColor * lodLandFadeFactor, lodLandBlendFactor);
	glossiness = lerp(glossiness, 0, lodLandBlendFactor);
#		endif
#	endif  // LOD_LAND_BLEND

#	if defined(SNOW) && !defined(TRUE_PBR)
	useSnowSpecular = landSnowMask != 0.0;
#	endif  // SNOW

#	if defined(BACK_LIGHTING)
	float4 backLightColor = TexBackLightSampler.Sample(SampBackLightSampler, uv);
#		if defined(HAIR) && defined(CS_HAIR)
	if (useHairFlowMap) {
		backLightColor = 0.0f;
	}
#		endif
#	endif  // BACK_LIGHTING

#	if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING) || defined(LOAD_SOFT_LIGHTING))
	float4 rimSoftLightColor = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
#	endif  // RIM_LIGHTING || SOFT_LIGHTING

	uint numLights = min(7, uint(NumLightNumShadowLight.x));
	uint numShadowLights = min(4, uint(NumLightNumShadowLight.y));

#	if defined(MODELSPACENORMALS) && !defined(SKINNED)
	float3 worldNormal = normal.xyz;
#	else
	float3 worldNormal = normalize(mul(tbn, normal.xyz));

#		if defined(SPARKLE)
	float3 projectedNormal = normalize(mul(tbn, float3(ProjectedUVParams2.xx * normal.xy, normal.z)));
#		endif  // SPARKLE
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
	float2 projNoiseUv = ProjectedUVParams.zz * input.TexCoord0.zw;
	float projNoise = TexCharacterLightProjNoiseSampler.Sample(SampCharacterLightProjNoiseSampler, projNoiseUv).x;
	float3 texProj = normalize(input.TexProj);
#		if defined(TREE_ANIM) || defined(LODOBJECTSHD)
	float vertexAlpha = 1;
#		else
	float vertexAlpha = input.Color.w;
#		endif  // defined (TREE_ANIM) || defined (LODOBJECTSHD)
	projWeight = -ProjectedUVParams.x * projNoise + (dot(worldNormal.xyz, texProj) * vertexAlpha - ProjectedUVParams.w);
#		if defined(LODOBJECTSHD)
	projWeight += (-0.5 + input.Color.w) * 2.5;
#		endif  // LODOBJECTSHD
#		if defined(SPARKLE)
	if (projWeight < 0)
		discard;

	worldNormal.xyz = projectedNormal;
#			if defined(SNOW)
	psout.Parameters.y = 1;
#			endif  // SNOW
#		elif !defined(FACEGEN) && !defined(MULTI_LAYER_PARALLAX) && !defined(PARALLAX) && !defined(SPARKLE)
	if (ProjectedUVParams3.w > 0.5) {
		float2 projNormalDiffuseUv = ProjectedUVParams3.x * projNoiseUv;
		float3 projNormal = TransformNormal(TexProjNormalSampler.Sample(SampProjNormalSampler, projNormalDiffuseUv).xyz);
		float2 projDetailNormalUv = ProjectedUVParams3.y * projNoiseUv;
		float3 projDetailNormal = TexProjDetail.Sample(SampProjDetailSampler, projDetailNormalUv).xyz;
		float3 finalProjNormal = normalize(TransformNormal(projDetailNormal) * float3(1, 1, projNormal.z) + float3(projNormal.xy, 0));
		float3 projBaseColor = TexProjDiffuseSampler.Sample(SampProjDiffuseSampler, projNormalDiffuseUv).xyz * ProjectedUVParams2.xyz;
		projectedMaterialWeight = smoothstep(0, 1, 5 * (0.1 + projWeight));
#			if defined(TRUE_PBR)
		projBaseColor = saturate(EnvmapData.xyz * projBaseColor);
		rawRMAOS.xyw = lerp(rawRMAOS.xyw, float3(ParallaxOccData.x, 0, ParallaxOccData.y), projectedMaterialWeight);
		float4 projectedGlintParameters = 0;
		if ((PBRFlags & PBR::Flags::ProjectedGlint) != 0) {
			projectedGlintParameters = SparkleParams;
		}
		glintParameters = lerp(glintParameters, projectedGlintParameters, projectedMaterialWeight);
#			elif defined(LOD_BLENDING) && (defined(LODOBJECTS) || defined(LODOBJECTSHD))
		projBaseColor.xyz *= SharedData::lodBlendingSettings.LODObjectSnowBrightness;
#			endif  // TRUE_PBR
		normal.xyz = lerp(normal.xyz, finalProjNormal, projectedMaterialWeight);
		baseColor.xyz = lerp(baseColor.xyz, projBaseColor, projectedMaterialWeight);

#			if defined(SNOW)
		useSnowDecalSpecular = true;
		psout.Parameters.y = GetSnowParameterY(projectedMaterialWeight, baseColor.w);
#			endif  // SNOW
	} else {
		if (projWeight > 0) {
			baseColor.xyz = ProjectedUVParams2.xyz;
#			if defined(SNOW)
			useSnowDecalSpecular = true;
			psout.Parameters.y = GetSnowParameterY(projWeight, baseColor.w);
#			endif  // SNOW
		} else {
#			if defined(SNOW)
			psout.Parameters.y = 0;
#			endif  // SNOW
		}
	}

#			if defined(SPECULAR)
	useSnowSpecular = useSnowDecalSpecular;
#			endif  // SPECULAR
#		endif      // SPARKLE

#	elif defined(SNOW)
#		if defined(LANDSCAPE)
	psout.Parameters.y = landSnowMask;
#		else
	psout.Parameters.y = baseColor.w;
#		endif  // LANDSCAPE
#	endif      // SNOW

#	if defined(WORLD_MAP)
	baseColor.xyz = GetWorldMapBaseColor(rawBaseColor.xyz, baseColor.xyz, projWeight);
#	endif  // WORLD_MAP

#	if defined(MODELSPACENORMALS)
	float3 vertexNormal = worldNormal;
#	endif

	float3 screenSpaceNormal = normalize(FrameBuffer::WorldToView(worldNormal, false, eyeIndex));

#	if defined(HAIR) && defined(CS_HAIR)
	float3 Bitangent = normalize(float3(input.TBN0.y, input.TBN1.y, input.TBN2.y));
	float3 hairT = 0;
#		if defined(BACK_LIGHTING)
	hairT = useHairFlowMap ? normalize(mul(tbn, sampledHairFlow)) : Bitangent;
#		else
	hairT = Bitangent;
#		endif
	hairT = Hair::ReorientTangent(hairT, worldNormal);

	if (SharedData::hairSpecularSettings.Enabled && SharedData::hairSpecularSettings.EnableTangentShift) {
		float3 shiftedNormal = Hair::ShiftWorldNormal(hairT, worldNormal, 0, uv);
		screenSpaceNormal = normalize(FrameBuffer::WorldToView(shiftedNormal, false, eyeIndex));
	}

	float3 transmissionColor = 0;
#	endif

#	if defined(TRUE_PBR)
	PBR::SurfaceProperties pbrSurfaceProperties = PBR::InitSurfaceProperties();

	pbrSurfaceProperties.Noise = screenNoise;

	pbrSurfaceProperties.Roughness = clamp(rawRMAOS.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
	pbrSurfaceProperties.Metallic = saturate(rawRMAOS.y);
	pbrSurfaceProperties.AO = rawRMAOS.z;
	pbrSurfaceProperties.F0 = lerp(saturate(rawRMAOS.w), Color::GammaToTrueLinear(baseColor.xyz), pbrSurfaceProperties.Metallic);

	pbrSurfaceProperties.GlintScreenSpaceScale = max(1, glintParameters.x);
	pbrSurfaceProperties.GlintLogMicrofacetDensity = clamp(PBR::Constants::MaxGlintDensity - glintParameters.y, PBR::Constants::MinGlintDensity, PBR::Constants::MaxGlintDensity);
	pbrSurfaceProperties.GlintMicrofacetRoughness = clamp(glintParameters.z, PBR::Constants::MinGlintRoughness, PBR::Constants::MaxGlintRoughness);
	pbrSurfaceProperties.GlintDensityRandomization = clamp(glintParameters.w, PBR::Constants::MinGlintDensityRandomization, PBR::Constants::MaxGlintDensityRandomization);

#		if defined(GLINT)
	float glintNoise = Random::R1Modified(float(SharedData::FrameCount), (Random::pcg2d(uint2(input.Position.xy)) / 4294967296.0).x);
	PBR::Glints::PrecomputeGlints(glintNoise, uvOriginal, ddx(uvOriginal), ddy(uvOriginal), pbrSurfaceProperties.GlintScreenSpaceScale, pbrSurfaceProperties.GlintCache);
#		endif

	baseColor.xyz *= 1 - pbrSurfaceProperties.Metallic;

	pbrSurfaceProperties.BaseColor = baseColor.xyz;

	float3 coatWorldNormal = worldNormal;

#		if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::Subsurface) != 0)
	{
		pbrSurfaceProperties.SubsurfaceColor = PBRParams2.xyz;
		pbrSurfaceProperties.Thickness = PBRParams2.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
		{
			float4 sampledSubsurfaceProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
			pbrSurfaceProperties.SubsurfaceColor *= Color::Diffuse(sampledSubsurfaceProperties.xyz);
			pbrSurfaceProperties.Thickness *= sampledSubsurfaceProperties.w;
		}
		pbrSurfaceProperties.Thickness = lerp(pbrSurfaceProperties.Thickness, 1, projectedMaterialWeight);
	}
	else if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		pbrSurfaceProperties.CoatColor = sampledCoatColor.xyz;
		pbrSurfaceProperties.CoatStrength = sampledCoatColor.w;
		pbrSurfaceProperties.CoatRoughness = MultiLayerParallaxData.x;
		pbrSurfaceProperties.CoatF0 = MultiLayerParallaxData.y;

		float2 coatUv = uv;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			coatUv = uvOriginal;
		}
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledCoatProperties = TexBackLightSampler.Sample(SampBackLightSampler, coatUv);
			pbrSurfaceProperties.CoatRoughness *= sampledCoatProperties.w;
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				coatWorldNormal = normalize(mul(tbn, TransformNormal(sampledCoatProperties.xyz)));
			}
		}
		pbrSurfaceProperties.CoatStrength = lerp(pbrSurfaceProperties.CoatStrength, 0, projectedMaterialWeight);
	}

	[branch] if ((PBRFlags & PBR::Flags::Fuzz) != 0)
	{
		pbrSurfaceProperties.FuzzColor = MultiLayerParallaxData.xyz;
		pbrSurfaceProperties.FuzzWeight = MultiLayerParallaxData.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledFuzzProperties = TexBackLightSampler.Sample(SampBackLightSampler, uv);
			pbrSurfaceProperties.FuzzColor *= Color::Diffuse(sampledFuzzProperties.xyz);
			pbrSurfaceProperties.FuzzWeight *= sampledFuzzProperties.w;
		}
		pbrSurfaceProperties.FuzzWeight = lerp(pbrSurfaceProperties.FuzzWeight, 0, projectedMaterialWeight);
	}
#		endif

	float3 specularColorPBR = 0;
	float3 transmissionColor = 0;

	float pbrGlossiness = 1 - pbrSurfaceProperties.Roughness;
#	endif  // TRUE_PBR

	float porosity = 1.0;

#	if defined(SKYLIGHTING)
#		if defined(VR)
	float3 positionMSSkylight = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#		else
	float3 positionMSSkylight = input.WorldPosition.xyz;
#		endif
#		if defined(DEFERRED)
	sh2 skylightingSH = Skylighting::sample(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, Skylighting::stbn_vec3_2Dx1D_128x128x64, input.Position.xy, positionMSSkylight, worldNormal);
#		else
	sh2 skylightingSH = inWorld ? Skylighting::sample(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, Skylighting::stbn_vec3_2Dx1D_128x128x64, input.Position.xy, positionMSSkylight, worldNormal) : float4(sqrt(4.0 * Math::PI), 0, 0, 0);
#		endif

#	endif

	float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
	float waterHeight = waterData.w;

	float waterRoughnessSpecular = 1;

#	if defined(WETNESS_EFFECTS)
	// Initialize wetness parameters
	float wetness = 0.0;
	float3 wetnessSpecular = 0.0;
	float3 wetnessNormal = worldNormal;

	// Calculate shore wetness factors
	float wetnessDistToWater = abs(input.WorldPosition.z - waterHeight);
	float shoreFactor = saturate(1.0 - (wetnessDistToWater / SharedData::wetnessEffectsSettings.ShoreRange));
	float shoreFactorAlbedo = (input.WorldPosition.z < waterHeight) ? 1.0 : shoreFactor;

	// Calculate wetness angle and occlusion
	float minWetnessValue = SharedData::wetnessEffectsSettings.MinRainWetness;
	float minWetnessAngle = saturate(max(minWetnessValue, worldNormal.z));
#		if defined(SKYLIGHTING)
	float wetnessOcclusion = inWorld ? pow(saturate(SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1))), 2) : 0.0;
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
#		if defined(SKINNED)
		float3 ripplePosition = input.ModelPosition.xyz;
#		elif defined(DEFERRED)
		float3 ripplePosition = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
#		else
		float3 ripplePosition = !FrameBuffer::FrameParams.y ? input.ModelPosition.xyz : input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
#		endif
		raindropInfo = WetnessEffects::GetRainDrops(ripplePosition, SharedData::wetnessEffectsSettings.Time, worldNormal, flatnessAmount);
	}

	// Calculate different wetness types
	float rainWetness = SharedData::wetnessEffectsSettings.Wetness * minWetnessAngle * SharedData::wetnessEffectsSettings.MaxRainWetness;
	rainWetness = max(rainWetness, raindropInfo.w);

#		if defined(SKIN) || defined(HAIR)
		rainWetness = SharedData::wetnessEffectsSettings.SkinWetness * SharedData::wetnessEffectsSettings.Wetness;
#		endif

	float shoreWetness = shoreFactor * SharedData::wetnessEffectsSettings.MaxShoreWetness;
	wetness = max(shoreWetness, rainWetness);

	// Calculate puddle effects
	float puddleWetness = SharedData::wetnessEffectsSettings.PuddleWetness * minWetnessAngle;
	float puddle = wetness;

#		if !defined(SKINNED)
	if (wetness > 0.0 || puddleWetness > 0.0) {
		float3 puddleCoords = ((input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz) * 0.5 + 0.5) * 0.01 / SharedData::wetnessEffectsSettings.PuddleRadius;
		puddle = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
		puddle = puddle * ((minWetnessAngle / SharedData::wetnessEffectsSettings.PuddleMaxAngle) * SharedData::wetnessEffectsSettings.MaxPuddleWetness * 0.25) + 0.5;
		puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
	}
#		endif

	// Apply occlusion and distance factors
	puddle *= wetnessOcclusion * nearFactor;

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

	waterRoughnessSpecular = 1.0 - wetnessGlossinessSpecular;
#	endif

	float3 dirLightColor = Color::Light(DirLightColor.xyz);
	float3 dirLightColorMultiplier = 1;

#	if defined(WATER_EFFECTS)
	dirLightColorMultiplier *= WaterEffects::ComputeCaustics(waterData, input.WorldPosition.xyz, eyeIndex);
#	endif

	float dirLightAngle = dot(worldNormal.xyz, DirLightDirection.xyz);

	if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir)) {
		dirLightColorMultiplier *= shadowColor.x;
	}
#	if !defined(DEFERRED)
	else if (!SharedData::InInterior && inWorld) {
		dirLightColorMultiplier *= ShadowSampling::GetLightingShadow(screenNoise, input.WorldPosition.xyz, eyeIndex);
	}
#	endif

#	if defined(SOFT_LIGHTING) || defined(BACK_LIGHTING) || defined(RIM_LIGHTING)
	bool inDirShadow = ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir) && shadowColor.x == 0);
#	else
	bool inDirShadow = ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir) && shadowColor.x == 0) && dirLightAngle > 0.0;
#	endif

	float3 refractedDirLightDirection = DirLightDirection;
#	if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		if (dot(DirLightDirection, coatWorldNormal) > 0)
			refractedDirLightDirection = -refract(-DirLightDirection, coatWorldNormal, eta);
	}
#	endif

	float dirDetailShadow = 1.0;
	float dirShadow = 1.0;
	float parallaxShadow = 1;

	bool inReflection = Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection;

#	if defined(SCREEN_SPACE_SHADOWS) && defined(DEFERRED)
	if (!SharedData::InInterior)
		dirDetailShadow = ScreenSpaceShadows::GetScreenSpaceShadow(input.Position.xyz, screenUV, screenNoise, eyeIndex);
#	endif

#	if defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
	[branch] if (inWorld && SharedData::extendedMaterialSettings.EnableShadows)
	{
		float3 dirLightDirectionTS = mul(refractedDirLightDirection, tbn).xyz;
#		if defined(LANDSCAPE)
#			if defined(TRUE_PBR)
	if (SharedData::extendedMaterialSettings.EnableParallax){
#			else
	if (SharedData::extendedMaterialSettings.EnableTerrainParallax || (SharedData::extendedMaterialSettings.EnableParallax && Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement)){
#			endif
#			if defined(TERRAIN_VARIATION)
			float weights[6];
			// Initialize weights array
			weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0.0;

			float sh0 = ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, parallaxShadowQuality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, weights);

			parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, dirLightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams, sharedOffset, dx, dy);
#			else
			// Standard terrain parallax shadow without stochastic sampling
			parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, dirLightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams);
#			endif
		}
#		elif defined(PARALLAX)
		[branch] if (SharedData::extendedMaterialSettings.EnableParallax)
			parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, lerp(parallaxShadowQuality, 1.0, SharedData::extendedMaterialSettings.ExtendShadows), screenNoise, displacementParams);
#		elif defined(EMAT_ENVMAP)
		[branch] if (complexMaterialParallax)
			parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexEnvMaskSampler, SampEnvMaskSampler, 3, lerp(parallaxShadowQuality, 1.0, SharedData::extendedMaterialSettings.ExtendShadows), screenNoise, displacementParams);
#		elif defined(TRUE_PBR) && !defined(LODLANDSCAPE)
		[branch] if (PBRParallax)
			parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, lerp(parallaxShadowQuality, 1.0, SharedData::extendedMaterialSettings.ExtendShadows), screenNoise, displacementParams);
#		endif  // LANDSCAPE
	}
#	endif  // defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

	if (dirShadow != 0.0 && (inWorld || inReflection))
		dirShadow *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);

	dirLightColorMultiplier *= dirShadow;

	float3 diffuseColor = 0.0.xxx;
	float3 specularColor = 0.0.xxx;

	float3 lightsDiffuseColor = 0.0.xxx;
	float3 coatLightsDiffuseColor = 0.0.xxx;
	float3 lightsSpecularColor = 0.0.xxx;

	float3 lodLandDiffuseColor = 0;

#	if defined(TRUE_PBR)
	{
		PBR::LightProperties lightProperties = PBR::InitLightProperties(dirLightColor, dirLightColorMultiplier * dirDetailShadow, parallaxShadow);
		float3 dirDiffuseColor, coatDirDiffuseColor, dirTransmissionColor, dirSpecularColor;
		PBR::GetDirectLightInput(dirDiffuseColor, coatDirDiffuseColor, dirTransmissionColor, dirSpecularColor, worldNormal.xyz, coatWorldNormal, refractedViewDirection, viewDirection, refractedDirLightDirection, DirLightDirection, lightProperties, pbrSurfaceProperties, tbnTr, uvOriginal);
		lightsDiffuseColor += dirDiffuseColor;
		coatLightsDiffuseColor += coatDirDiffuseColor;
		transmissionColor += dirTransmissionColor;
		specularColorPBR += dirSpecularColor * !SharedData::InInterior;
#		if defined(LOD_LAND_BLEND)
		lodLandDiffuseColor += dirLightColor / Math::PI * saturate(dirLightAngle) * dirLightColorMultiplier * dirDetailShadow * parallaxShadow;
#		endif
#		if defined(WETNESS_EFFECTS)
		if (waterRoughnessSpecular < 1.0)
			specularColorPBR += PBR::GetWetnessDirectLightSpecularInput(wetnessNormal, viewDirection, DirLightDirection, lightProperties.CoatLightColor, waterRoughnessSpecular) * wetnessGlossinessSpecular;
#		endif
	}
#	else
	dirDetailShadow *= parallaxShadow;
	dirLightColor *= dirLightColorMultiplier;

	float3 dirDiffuseColor = dirLightColor * saturate(dirLightAngle) * dirDetailShadow;

#		if defined(SOFT_LIGHTING)
	lightsDiffuseColor += dirLightColor * GetSoftLightMultiplier(dirLightAngle) * rimSoftLightColor.xyz;
#		endif

#		if defined(RIM_LIGHTING)
	lightsDiffuseColor += dirLightColor * GetRimLightMultiplier(DirLightDirection, viewDirection, worldNormal.xyz) * rimSoftLightColor.xyz;
#		endif

#		if defined(BACK_LIGHTING)
	lightsDiffuseColor += dirLightColor * saturate(-dirLightAngle) * backLightColor.xyz;
#		endif

	if (useSnowSpecular && useSnowDecalSpecular) {
#		if defined(SNOW)
		lightsSpecularColor += GetSnowSpecularColor(input, worldNormal.xyz, viewDirection);
#		endif
	} else {
#		if defined(HAIR) && defined(CS_HAIR)
		if (SharedData::hairSpecularSettings.Enabled) {
			float3 dirTransmissionColor = 0.0;
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, DirLightDirection, screenNoise, eyeIndex);
			Hair::GetHairDirectLight(dirDiffuseColor, lightsSpecularColor, dirTransmissionColor, hairT, DirLightDirection, viewDirection, worldNormal.xyz, vertexNormal.xyz, dirLightColor.xyz * dirDetailShadow, SharedData::hairSpecularSettings.HairGlossiness, hairShadow, uv, baseColor.xyz);
			transmissionColor += dirTransmissionColor;
		}
		else {
#			if defined(SPECULAR)
			lightsSpecularColor = GetLightSpecularInput(input, DirLightDirection, viewDirection, worldNormal.xyz, dirLightColor.xyz * dirDetailShadow, shininess, uv);
#			endif
		}
#		elif defined(SPECULAR) || defined(SPARKLE)
		lightsSpecularColor = GetLightSpecularInput(input, DirLightDirection, viewDirection, worldNormal.xyz, dirLightColor.xyz * dirDetailShadow, shininess, uv);
#		endif
	}

	lightsDiffuseColor += dirDiffuseColor;

#		if defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1.0)
		wetnessSpecular += WetnessEffects::GetWetnessSpecular(wetnessNormal, DirLightDirection, viewDirection, dirLightColor * dirDetailShadow, waterRoughnessSpecular);
#		endif
#	endif

#	if !defined(LOD)
#		if !defined(LIGHT_LIMIT_FIX)
	[loop] for (uint lightIndex = 0; lightIndex < numLights; lightIndex++)
	{
		float3 lightDirection = PointLightPosition[eyeIndex * numLights + lightIndex].xyz - input.WorldPosition.xyz;
		float lightDist = length(lightDirection);
		float intensityFactor = saturate(lightDist / PointLightPosition[lightIndex].w);
		if (intensityFactor == 1)
			continue;

		float intensityMultiplier = 1 - intensityFactor * intensityFactor;
		float3 lightColor = Color::Light(PointLightColor[lightIndex].xyz) * intensityMultiplier;
		float lightShadow = 1.f;
		if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) {
			if (lightIndex < numShadowLights) {
				lightShadow *= shadowColor[ShadowLightMaskSelect[lightIndex]];
			}
		}

		float3 normalizedLightDirection = normalize(lightDirection);

#			if defined(TRUE_PBR)
		{
			float3 pointDiffuseColor, coatPointDiffuseColor, pointTransmissionColor, pointSpecularColor;
			float3 refractedLightDirection = normalizedLightDirection;
#				if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
			{
				if (dot(normalizedLightDirection, coatWorldNormal) > 0)
					refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
			}
#				endif
			PBR::LightProperties lightProperties = PBR::InitLightProperties(lightColor, lightShadow, 1);
			PBR::GetDirectLightInput(pointDiffuseColor, coatPointDiffuseColor, pointTransmissionColor, pointSpecularColor, worldNormal.xyz, coatWorldNormal, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightProperties, pbrSurfaceProperties, tbnTr, uvOriginal);
			lightsDiffuseColor += pointDiffuseColor;
			coatLightsDiffuseColor += coatPointDiffuseColor;
			transmissionColor += pointTransmissionColor;
			specularColorPBR += pointSpecularColor;
		}
#			else
		lightColor *= lightShadow;
		float lightAngle = dot(worldNormal.xyz, normalizedLightDirection.xyz);
		float3 lightDiffuseColor = lightColor * saturate(lightAngle.xxx);

#				if defined(SOFT_LIGHTING)
		lightDiffuseColor += lightColor * GetSoftLightMultiplier(lightAngle) * rimSoftLightColor.xyz;
#				endif  // SOFT_LIGHTING

#				if defined(RIM_LIGHTING)
		lightDiffuseColor += lightColor * GetRimLightMultiplier(normalizedLightDirection, viewDirection, worldNormal.xyz) * rimSoftLightColor.xyz;
#				endif  // RIM_LIGHTING

#				if defined(BACK_LIGHTING)
		lightDiffuseColor += lightColor * saturate(-lightAngle) * backLightColor.xyz;
#				endif  // BACK_LIGHTING
#				if defined(HAIR) && defined(CS_HAIR)
		if (SharedData::hairSpecularSettings.Enabled) {
			float3 lightSpecularColor = 0;
			float3 lightTransmissionColor = 0;
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, normalizedLightDirection, screenNoise, eyeIndex);
			Hair::GetHairDirectLight(lightDiffuseColor, lightSpecularColor, lightTransmissionColor, hairT, normalizedLightDirection, viewDirection, worldNormal.xyz, vertexNormal.xyz, lightColor, SharedData::hairSpecularSettings.HairGlossiness, hairShadow, uv, baseColor.xyz);
			lightsSpecularColor += lightSpecularColor;
			transmissionColor += lightTransmissionColor;
		} else {
#					if defined(SPECULAR)
			lightsSpecularColor += GetLightSpecularInput(input, normalizedLightDirection, viewDirection, worldNormal.xyz, lightColor, shininess, uv);
#					endif
		}
#				elif defined(SPECULAR) || (defined(SPARKLE) && !defined(SNOW))
		lightsSpecularColor += GetLightSpecularInput(input, normalizedLightDirection, viewDirection, worldNormal.xyz, lightColor, shininess, uv);
#				endif  // defined (SPECULAR) || (defined (SPARKLE) && !defined(SNOW))

		lightsDiffuseColor += lightDiffuseColor;
#			endif
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

		float3 lightColor = Color::Light(light.color.xyz) * intensityMultiplier;
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
#			if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			if (dot(normalizedLightDirection, coatWorldNormal) > 0)
				refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
		}
#			endif

		float parallaxShadow = 1;

#			if defined(EMAT)
		[branch] if (
			SharedData::extendedMaterialSettings.EnableShadows &&
			!(light.lightFlags & LightLimitFix::LightFlags::Simple) &&
			lightAngle > 0.0 &&
			shadowComponent != 0.0)
		{
			float3 lightDirectionTS = normalize(mul(refractedLightDirection, tbn).xyz);
#				if defined(PARALLAX)
			[branch] if (SharedData::extendedMaterialSettings.EnableParallax)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#				elif defined(LANDSCAPE)
#					if defined(TRUE_PBR)
					if (SharedData::extendedMaterialSettings.EnableParallax)
#					else
					if (SharedData::extendedMaterialSettings.EnableTerrainParallax || (SharedData::extendedMaterialSettings.EnableParallax && Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement))
#					endif
#					if defined(TERRAIN_VARIATION)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, lightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams, sharedOffset, dx, dy);
#					else
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, lightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams);
#					endif
#				elif defined(EMAT_ENVMAP)
			[branch] if (complexMaterialParallax)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexEnvMaskSampler, SampEnvMaskSampler, 3, parallaxShadowQuality, screenNoise, displacementParams);
#				elif defined(TRUE_PBR) && !defined(LODLANDSCAPE)
			[branch] if (PBRParallax)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#				endif
		}
#			endif

#			if defined(TRUE_PBR)
		{
			PBR::LightProperties lightProperties = PBR::InitLightProperties(lightColor, lightShadow, parallaxShadow);
			float3 pointDiffuseColor, coatPointDiffuseColor, pointTransmissionColor, pointSpecularColor;
			PBR::GetDirectLightInput(pointDiffuseColor, coatPointDiffuseColor, pointTransmissionColor, pointSpecularColor, worldNormal.xyz, coatWorldNormal, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightProperties, pbrSurfaceProperties, tbnTr, uvOriginal);
			lightsDiffuseColor += pointDiffuseColor;
			coatLightsDiffuseColor += coatPointDiffuseColor;
			transmissionColor += pointTransmissionColor;
			specularColorPBR += pointSpecularColor;
#				if defined(WETNESS_EFFECTS)
			if (waterRoughnessSpecular < 1.0)
				specularColorPBR += PBR::GetWetnessDirectLightSpecularInput(wetnessNormal, viewDirection, normalizedLightDirection, lightProperties.CoatLightColor, waterRoughnessSpecular) * wetnessGlossinessSpecular;
#				endif
		}
#			else
		lightColor *= lightShadow;

		float3 lightDiffuseColor = lightColor * parallaxShadow * saturate(lightAngle.xxx);
		float lightBacklighting = 1.0 + saturate(dot(normalizedLightDirection.xyz, viewDirection));

#				if defined(SOFT_LIGHTING)
		lightDiffuseColor += lightBacklighting * lightColor * GetSoftLightMultiplier(lightAngle) * rimSoftLightColor.xyz;
#				endif

#				if defined(RIM_LIGHTING)
		lightDiffuseColor += lightBacklighting * lightColor * GetRimLightMultiplier(normalizedLightDirection, viewDirection, worldNormal.xyz) * rimSoftLightColor.xyz;
#				endif

#				if defined(BACK_LIGHTING)
		lightDiffuseColor += lightBacklighting * lightColor * saturate(-lightAngle) * backLightColor.xyz;
#				endif

#				if defined(HAIR) && defined(CS_HAIR) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
		if (SharedData::hairSpecularSettings.Enabled) {
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, normalizedLightDirection, screenNoise, eyeIndex);
			float3 lightSpecularColor = 0;
			float3 lightTransmissionColor = 0;
			Hair::GetHairDirectLight(lightDiffuseColor, lightSpecularColor, lightTransmissionColor, hairT, normalizedLightDirection, viewDirection, worldNormal.xyz, vertexNormal.xyz, lightColor, SharedData::hairSpecularSettings.HairGlossiness, hairShadow, uv, baseColor.xyz);
			lightsSpecularColor += lightSpecularColor;
			transmissionColor += lightTransmissionColor;
		} else {
#					if defined(SPECULAR)
			lightsSpecularColor += GetLightSpecularInput(input, normalizedLightDirection, viewDirection, worldNormal.xyz, lightColor, shininess, uv);
#					endif
		}
#				elif defined(SPECULAR) || (defined(SPARKLE) && !defined(SNOW))
		lightsSpecularColor += GetLightSpecularInput(input, normalizedLightDirection, viewDirection, worldNormal.xyz, lightColor, shininess, uv);
#				endif

		lightsDiffuseColor += lightDiffuseColor;
#			endif

#			if defined(WETNESS_EFFECTS)
		if (waterRoughnessSpecular < 1.0)
			wetnessSpecular += WetnessEffects::GetWetnessSpecular(wetnessNormal, normalizedLightDirection, viewDirection, lightColor, waterRoughnessSpecular);
#			endif
	}
#		endif
#	endif

	diffuseColor += lightsDiffuseColor;
	specularColor += lightsSpecularColor;

#	if !defined(LANDSCAPE)
	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::CharacterLight) {
		float charLightMul = saturate(dot(viewDirection, worldNormal.xyz)) * CharacterLightParams.x + CharacterLightParams.y * saturate(dot(float2(0.164398998, -0.986393988), worldNormal.yz));
		float charLightColor = min(CharacterLightParams.w, max(0, CharacterLightParams.z * TexCharacterLightProjNoiseSampler.Sample(SampCharacterLightProjNoiseSampler, baseShadowUV).x));
		diffuseColor += (charLightMul * charLightColor).xxx;
	}
#	endif

#	if defined(EYE)
	worldNormal.xyz = input.EyeNormal;
#	endif  // EYE

	float3 emitColor = EmitColor;
#	if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	bool hasEmissive = (0x3F & (Permutation::PixelShaderDescriptor >> 24)) == Permutation::LightingTechnique::Glowmap;
#		if defined(TRUE_PBR)
	hasEmissive = hasEmissive || (PBRFlags & PBR::Flags::HasEmissive != 0);
#		endif
	[branch] if (hasEmissive)
	{
		float3 glowColor = Color::Diffuse(TexGlowSampler.Sample(SampGlowSampler, uv).xyz);
		emitColor *= glowColor;
	}
#	endif

#	if !defined(TRUE_PBR)
	diffuseColor += emitColor.xyz;
#	endif

	float3 ambientNormal = worldNormal;
#	if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled && SharedData::hairSpecularSettings.HairMode == 1)
		ambientNormal = normalize(viewDirection - hairT * dot(viewDirection, hairT));
#	endif

	float3 directionalAmbientColor = max(0, mul(DirectionalAmbient, float4(ambientNormal, 1.0)));
	float3 directionalAmbientColorAdditive = 0.0;

#	if defined(SKYLIGHTING)
	float skylightingDiffuse = 1;
	float skylightingFadeOutFactor = 1.0;
	if (!SharedData::InInterior) {
		skylightingFadeOutFactor = Skylighting::getFadeOutFactor(input.WorldPosition.xyz);
		skylightingDiffuse = SphericalHarmonics::Unproject(skylightingSH, ambientNormal);
		skylightingDiffuse = saturate(skylightingDiffuse);
		skylightingDiffuse = lerp(1.0, skylightingDiffuse, skylightingFadeOutFactor);
		skylightingDiffuse = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylightingDiffuse);
	}
#	endif

#	if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL && inWorld) {
		float3 iblColor = ImageBasedLighting::GetIBLColor(-ambientNormal);
		directionalAmbientColor += iblColor * SharedData::enbSettings.IBLMultiplicativeAmount;		
		directionalAmbientColorAdditive += iblColor * SharedData::enbSettings.IBLAdditiveAmount;
	}
#	endif

	float3 reflectionDiffuseColor = diffuseColor + directionalAmbientColor;

#	if defined(TRUE_PBR) && defined(LOD_LAND_BLEND) && !defined(DEFERRED)
	lodLandDiffuseColor += directionalAmbientColor;
#	endif

#	if !defined(TRUE_PBR)
#		if defined(HAIR) && defined(CS_HAIR)
	if (!SharedData::hairSpecularSettings.Enabled)
		diffuseColor += directionalAmbientColor;
#		else
	diffuseColor += directionalAmbientColor;
#		endif
#	endif

#	if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float envMask = EnvmapData.x * MaterialData.x;

	float viewNormalAngle = dot(worldNormal.xyz, viewDirection);
	float3 envSamplingPoint = (viewNormalAngle * 2) * worldNormal.xyz - viewDirection;

	if (envMask > 0.0) {
		if (EnvmapData.y) {
			envMask *= TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv).x;
		} else {
			envMask *= glossiness;
		}
	}

	float3 envColor = 0.0;
	bool dynamicCubemap = false;

#		if defined(DYNAMIC_CUBEMAPS)
	float3 F0 = 0.0;
	float envRoughness = 1.0;
#		endif

	if (envMask > 0.0) {
#		if defined(DYNAMIC_CUBEMAPS)
		uint2 envSize;
		TexEnvSampler.GetDimensions(envSize.x, envSize.y);

#			if defined(EMAT)
		if (envSize.x == 1 && envSize.y == 1 || complexMaterial) {
#			else
		if (envSize.x == 1 && envSize.y == 1) {
#			endif

			dynamicCubemap = true;

#			if defined(EMAT)
			if (!complexMaterial)
#			endif
			{
				// Dynamic Cubemap Creator sets this value to black, if it is anything but black it is wrong
				float3 envColorTest = TexEnvSampler.SampleLevel(SampEnvSampler, float3(0.0, 1.0, 0.0), 15).xyz;
				dynamicCubemap = all(envColorTest == 0.0);
			}

#			if defined(CREATOR)
			if (SharedData::cubemapCreatorSettings.Enabled) {
				dynamicCubemap = true;
			}
#			endif

			if (dynamicCubemap) {
				float4 envColorBase = TexEnvSampler.SampleLevel(SampEnvSampler, float3(1.0, 0.0, 0.0), 15);

				if (envColorBase.a < 1.0) {
					F0 = Color::GammaToLinear(envColorBase.rgb);
					envRoughness = envColorBase.a;
				} else {
					F0 = 1.0;
					envRoughness = 1.0 / 7.0;
				}

#			if defined(CREATOR)
				if (SharedData::cubemapCreatorSettings.Enabled) {
					F0 = SharedData::cubemapCreatorSettings.CubemapColor.rgb;
					envRoughness = SharedData::cubemapCreatorSettings.CubemapColor.a;
				}
#			endif

#			if defined(EMAT)
				float complexMaterialRoughness = 1.0 - complexMaterialColor.y;
				envRoughness = lerp(envRoughness, complexMaterialRoughness, complexMaterial);
				F0 = lerp(F0, complexSpecular, complexMaterial);
#			endif

				if (any(F0 > 0.0))
#			if defined(SKYLIGHTING)
					envColor = DynamicCubemaps::GetDynamicCubemap(worldNormal, vertexNormal, viewDirection, envRoughness, F0, skylightingSH) * envMask;
#			else
					envColor = DynamicCubemaps::GetDynamicCubemap(worldNormal, vertexNormal, viewDirection, envRoughness, F0) * envMask;
#			endif
				else
					envColor = 0.0;
			}
		}
#		endif

		if (!dynamicCubemap) {
			float3 envColorBase = Color::GammaToLinear(TexEnvSampler.Sample(SampEnvSampler, envSamplingPoint).xyz);
			envColor = envColorBase.xyz * envMask;
		}
	}

#	endif  // defined (ENVMAP) || defined (MULTI_LAYER_PARALLAX) || defined(EYE)

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);

#	if defined(WETNESS_EFFECTS)
#		if !(defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(EYE)) || defined(TREE_ANIM)
#			if defined(TRUE_PBR)
#				if !defined(LANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		porosity = 0;
	}
	else
#				endif
	{
		porosity = lerp(porosity, 0.0, saturate(sqrt(pbrSurfaceProperties.Metallic)));
	}
#			elif defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX)
	porosity = lerp(porosity, 0.0, saturate(sqrt(envMask)));
#			endif
	float wetnessDarkeningAmount = porosity * wetnessGlossinessAlbedo;
	baseColor.xyz = lerp(baseColor.xyz, pow(abs(baseColor.xyz), 1.0 + wetnessDarkeningAmount), 0.5);
#		endif

#		if defined(DYNAMIC_CUBEMAPS)
#			if defined(SKYLIGHTING)
	float3 wetnessReflectance = DynamicCubemaps::GetDynamicCubemap(wetnessNormal, vertexNormal, viewDirection, waterRoughnessSpecular, 0.02 * wetnessGlossinessSpecular, skylightingSH);
#			else
	float3 wetnessReflectance = DynamicCubemaps::GetDynamicCubemap(wetnessNormal, vertexNormal, viewDirection, waterRoughnessSpecular, 0.02 * wetnessGlossinessSpecular);
#			endif
#		else
	float3 wetnessReflectance = 0.0;
#		endif

#		if !defined(DEFERRED)
	wetnessSpecular += wetnessReflectance;
#		endif
#	endif

#	if defined(HAIR)
	float3 vertexColor = lerp(1, TintColor.xyz, input.Color.y);
#		if defined(CS_HAIR)
	float3 indirectDiffuseLobeWeight, indirectSpecularLobeWeightPrim, indirectSpecularLobeWeightSec;
	if (SharedData::hairSpecularSettings.Enabled)
		vertexColor = 1;
	Hair::GetHairIndirectSpecularLobeWeights(indirectDiffuseLobeWeight, indirectSpecularLobeWeightPrim, indirectSpecularLobeWeightSec, hairT, worldNormal.xyz, viewDirection, vertexNormal, SharedData::hairSpecularSettings.HairGlossiness, uv, baseColor.xyz);
	indirectDiffuseLobeWeight *= SharedData::hairSpecularSettings.DiffuseIndirectMult;
	indirectSpecularLobeWeightPrim *= SharedData::hairSpecularSettings.SpecularIndirectMult;
	indirectSpecularLobeWeightSec *= SharedData::hairSpecularSettings.SpecularIndirectMult;
#		endif  // CS_HAIR
#	elif defined(SKYLIGHTING)
	float3 vertexColor = input.Color.xyz;
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);

	if (!SharedData::InInterior) {
#		if defined(LANDSCAPE)
		// Remove AO
		vertexColor = vertexColor / vertexAO;
#			if defined(LOD_BLENDING)
		vertexColor = lerp(vertexColor, 1, SharedData::lodBlendingSettings.DisableTerrainVertexColors);
#			endif  // LOD_BLENDING
#		else

		if (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsTree) {
			// Remove AO
			float3 originalVertexColor = vertexColor;
			vertexColor = lerp(vertexColor, vertexColor / vertexAO, sqrt(vertexAO));
			vertexColor = lerp(input.Color.xyz, vertexColor, skylightingFadeOutFactor);

			// Apply AO to direct lighting only
			float3 originalDiffuseColor = diffuseColor;
			diffuseColor -= lightsDiffuseColor;
			diffuseColor += lerp(lightsDiffuseColor, lightsDiffuseColor * vertexAO, skylightingFadeOutFactor);

			vertexColor = lerp(vertexColor, originalVertexColor, SharedData::skylightingSettings.MinDiffuseVisibility);
			diffuseColor = lerp(diffuseColor, originalDiffuseColor, SharedData::skylightingSettings.MinDiffuseVisibility);
		}

		// Brighten skylighting on vertex AO
		vertexColor *= 1.0 + (1.0 - vertexAO) * (1.0 - skylightingDiffuse);
#		endif
	}
#	else
	float3 vertexColor = input.Color.xyz;
#		if defined(LANDSCAPE) && defined(LOD_BLENDING)
		vertexColor = lerp(vertexColor, 1, SharedData::lodBlendingSettings.DisableTerrainVertexColors);
#		endif  // LOD_BLENDING
#	endif  // defined (HAIR)

	float4 color = 0;

#	if defined(TRUE_PBR)
	{
		float3 directLightsDiffuseInput = diffuseColor * baseColor.xyz;
		[branch] if ((PBRFlags & PBR::Flags::ColoredCoat) != 0)
		{
			directLightsDiffuseInput = lerp(directLightsDiffuseInput, pbrSurfaceProperties.CoatColor * coatLightsDiffuseColor, pbrSurfaceProperties.CoatStrength);
		}

		color.xyz += directLightsDiffuseInput;
	}

	float3 indirectDiffuseLobeWeight, indirectSpecularLobeWeight;
	PBR::GetIndirectLobeWeights(indirectDiffuseLobeWeight, indirectSpecularLobeWeight, worldNormal.xyz, viewDirection, vertexNormal, baseColor.xyz, pbrSurfaceProperties);
#		if defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1.0)
		indirectSpecularLobeWeight = max(indirectSpecularLobeWeight, PBR::GetWetnessIndirectSpecularLobeWeight(wetnessNormal, viewDirection, vertexNormal, waterRoughnessSpecular));
#		endif

	color.xyz += indirectDiffuseLobeWeight * directionalAmbientColor;

#		if !defined(DEFERRED)
#			if defined(DYNAMIC_CUBEMAPS)
#				if defined(SKYLIGHTING)
	specularColorPBR += indirectSpecularLobeWeight * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(screenUV, worldNormal, vertexNormal, viewDirection, pbrSurfaceProperties.Roughness, skylightingSH);
#				else
	specularColorPBR += indirectSpecularLobeWeight * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(screenUV, worldNormal, vertexNormal, viewDirection, pbrSurfaceProperties.Roughness);
#				endif
#			else
	specularColorPBR += indirectSpecularLobeWeight * directionalAmbientColor;
#			endif
#		else
	indirectDiffuseLobeWeight *= vertexColor;
#		endif

	// Fixes white items in UI for VR
	[branch] if ((PBRFlags & PBR::Flags::HasEmissive) != 0)
	{
		color.xyz += emitColor.xyz;
	}
	color.xyz += transmissionColor;
#	elif defined(HAIR) && defined(CS_HAIR)
	color.xyz += diffuseColor * baseColor.xyz;
	if (SharedData::hairSpecularSettings.Enabled) {
		color.xyz += indirectDiffuseLobeWeight * directionalAmbientColor;
		color.xyz += transmissionColor;
	}
#	else
	color.xyz += diffuseColor * baseColor.xyz;
#	endif

	color.xyz += directionalAmbientColorAdditive;

#	if defined(HAIR) && defined(CS_HAIR)
#		if !defined(DEFERRED)
#			if defined(DYNAMIC_CUBEMAPS)
	if (SharedData::hairSpecularSettings.Enabled && SharedData::hairSpecularSettings.HairMode != 1)
#				if defined(SKYLIGHTING)
	{
		float3 indirectSpecular = Hair::GetHairDynamicCubemapSpecularIrradiance(uv, screenUV, hairT, worldNormal, vertexNormal, viewDirection, SharedData::hairSpecularSettings.HairGlossiness, indirectSpecularLobeWeightPrim, indirectSpecularLobeWeightSec, skylightingSH);
		color.xyz += indirectSpecular;
	}
#				else
	{
		float3 indirectSpecular = Hair::GetHairDynamicCubemapSpecularIrradiance(uv, screenUV, hairT, worldNormal, vertexNormal, viewDirection, SharedData::hairSpecularSettings.HairGlossiness, indirectSpecularLobeWeightPrim, indirectSpecularLobeWeightSec);
		color.xyz += indirectSpecular;
	}
#				endif
#			endif
#		endif
#	endif

	color.xyz *= vertexColor;

#	if defined(MULTI_LAYER_PARALLAX)
	float layerValue = MultiLayerParallaxData.x * TexLayerSampler.Sample(SampLayerSampler, uv).w;
	float3 tangentViewDirection = mul(viewDirection, tbn);
	float3 layerNormal = MultiLayerParallaxData.yyy * (normalColor.xyz * 2.0.xxx + float3(-1, -1, -2)) + float3(0, 0, 1);
	float layerViewAngle = dot(-tangentViewDirection.xyz, layerNormal.xyz) * 2;
	float3 layerViewProjection = -layerNormal.xyz * layerViewAngle.xxx - tangentViewDirection.xyz;
	float2 layerUv = uv * MultiLayerParallaxData.zw + (0.0009765625 * (layerValue / abs(layerViewProjection.z))).xx * layerViewProjection.xy;

	float3 layerColor = TexLayerSampler.Sample(SampLayerSampler, layerUv).xyz;

	float mlpBlendFactor = saturate(viewNormalAngle) * (1.0 - baseColor.w);

	color.xyz = lerp(color.xyz, diffuseColor * vertexColor * layerColor, mlpBlendFactor);

#		if defined(DEFERRED)
	baseColor.xyz *= 1.0 - mlpBlendFactor;
#		endif
#	endif  // MULTI_LAYER_PARALLAX

#	if defined(SPECULAR)
# 		if defined(HAIR) && defined(CS_HAIR)
	if (!SharedData::hairSpecularSettings.Enabled)
#		endif
		specularColor = (specularColor * glossiness * MaterialData.yyy) * SpecularColor.xyz;
#	elif defined(SPARKLE)
	specularColor *= glossiness;
#	endif  // SPECULAR

#	if defined(SNOW)
	if (useSnowSpecular)
		specularColor = 0;
#	endif

	specularColor = Color::GammaToLinear(specularColor);

	diffuseColor = reflectionDiffuseColor;

#	if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#		if defined(DYNAMIC_CUBEMAPS)
	if (!dynamicCubemap)
#		endif
		specularColor += envColor * Color::GammaToLinear(diffuseColor);
#	endif

#	if defined(EMAT_ENVMAP)
	specularColor *= complexSpecular;
#	endif  // defined (EMAT) && defined(ENVMAP)

#	if !defined(DEFERRED) && defined(DYNAMIC_CUBEMAPS) && (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
	if (dynamicCubemap)
		specularColor += envColor;
#	endif

#	if defined(WETNESS_EFFECTS) && !defined(TRUE_PBR)
	specularColor += wetnessSpecular * wetnessGlossinessSpecular;
#	endif

#	if defined(LOD_LAND_BLEND) && defined(TRUE_PBR)
	{
		lodLandDiffuseColor += directionalAmbientColor;
		float3 litLodLandColor = vertexColor * lodLandColor.xyz * lodLandFadeFactor * lodLandDiffuseColor;
		color.xyz = lerp(color.xyz * Color::PBRLightingScale, litLodLandColor, lodLandBlendFactor);

		specularColor = lerp(specularColorPBR * Color::PBRLightingScale, 0, lodLandBlendFactor);
		indirectDiffuseLobeWeight = lerp(indirectDiffuseLobeWeight * Color::PBRLightingScale, vertexColor * lodLandColor.xyz * lodLandFadeFactor, lodLandBlendFactor);
		indirectSpecularLobeWeight = lerp(indirectSpecularLobeWeight, 0, lodLandBlendFactor);
		pbrGlossiness = lerp(pbrGlossiness, 0, lodLandBlendFactor);
	}
#	elif defined(TRUE_PBR)
	color.xyz *= Color::PBRLightingScale;
	specularColorPBR *= Color::PBRLightingScale;
	indirectDiffuseLobeWeight *= Color::PBRLightingScale;
	specularColor = specularColorPBR;
#	endif

	float3 outputAlbedo = baseColor.xyz * vertexColor;

#		if defined(TRUE_PBR)
	outputAlbedo = indirectDiffuseLobeWeight;
#		endif

#		if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		outputAlbedo = indirectDiffuseLobeWeight;
	}
#		endif

	directionalAmbientColor *= outputAlbedo;
	directionalAmbientColor += directionalAmbientColorAdditive;

#	if defined(SKYLIGHTING)
	Skylighting::applySkylighting(color.xyz, directionalAmbientColor, outputAlbedo, skylightingDiffuse);
#	endif

#	if !defined(DEFERRED)
	color.xyz = Color::LinearToGamma(Color::GammaToLinear(color.xyz) + specularColor);
	float3 fogColor = input.FogParam.xyz;
	if (FrameBuffer::FrameParams.y && FrameBuffer::FrameParams.z)
		color.xyz = lerp(color.xyz, fogColor, input.FogParam.w);
#	endif

#	if defined(TESTCUBEMAP) && defined(ENVMAP) && defined(DYNAMIC_CUBEMAPS)
	baseColor.xyz = 0.0;
	specularColor = 0.0;
	diffuseColor = 0.0;
	dynamicCubemap = true;
	envColor = 1.0;
	envRoughness = 0.0;
	color.xyz = 0;
#	endif

#	if defined(LANDSCAPE) && !defined(LOD_LAND_BLEND)
	psout.Diffuse.w = 0;
#	else
	float alpha = baseColor.w;
#		if defined(EMAT) && !defined(LANDSCAPE)
#			if defined(PARALLAX)
	alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
#			elif defined(TRUE_PBR)
	[branch] if (PBRParallax)
	{
		alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
	}
#			endif
#		endif
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
#		if !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
	alpha *= input.Color.w;
#		endif  // !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
#		if defined(DO_ALPHA_TEST)
#			if defined(DEPTH_WRITE_DECALS)
	if (alpha - 0.0156862754 < 0) {
		discard;
	}
	alpha = saturate(1.05 * alpha);
#			endif  // DEPTH_WRITE_DECALS
	if (alpha - AlphaTestRefRS < 0) {
		discard;
	}
#		endif      // DO_ALPHA_TEST

#		if defined(ANISOTROPIC_ALPHA)
	// Uniform alpha material settings
	uint AlphaMaterialModel = ExtendedTranslucency::GetMaterialModelFromDescriptor(Permutation::ExtraFeatureDescriptor);
	float AlphaMaterialReduction = 0.f;
	float AlphaMaterialSoftness = 0.f;
	float AlphaMaterialStrength = 0.f;
	[branch] if (AlphaMaterialModel == ExtendedTranslucency::MaterialModel::Default) {
		AlphaMaterialModel = SharedData::extendedTranslucencySettings.MaterialModel;
		AlphaMaterialReduction = SharedData::extendedTranslucencySettings.Reduction;
		AlphaMaterialSoftness = SharedData::extendedTranslucencySettings.Softness;
		AlphaMaterialStrength = SharedData::extendedTranslucencySettings.Strength;
	}

	[branch] if (ExtendedTranslucency::IsValidMaterial(AlphaMaterialModel))
	{
		if (alpha >= 0.0156862754 && alpha < 1.0) {
			float originalAlpha = alpha;
			alpha = alpha * (1.0 - AlphaMaterialReduction);
			[branch] if (AlphaMaterialModel == ExtendedTranslucency::MaterialModel::AnisotropicFabric)
			{
#			if defined(SKINNED) || !defined(MODELSPACENORMALS)
				alpha = ExtendedTranslucency::GetViewDependentAlphaFabric2D(alpha, viewDirection, tbnTr);
#			else
				alpha = ExtendedTranslucency::GetViewDependentAlphaFabric1D(alpha, viewDirection, worldNormal.xyz);
#			endif
			}
			else if (AlphaMaterialModel == ExtendedTranslucency::MaterialModel::IsotropicFabric)
			{
				alpha = ExtendedTranslucency::GetViewDependentAlphaFabric1D(alpha, viewDirection, worldNormal.xyz);
			}
			else
			{
				alpha = ExtendedTranslucency::GetViewDependentAlphaNaive(alpha, viewDirection, worldNormal.xyz);
			}
			alpha = saturate(ExtendedTranslucency::SoftClamp(alpha, 2.0f - AlphaMaterialSoftness));
			alpha = lerp(alpha, originalAlpha, AlphaMaterialStrength);
		}
	}
#		endif  // ANISOTROPIC_ALPHA

	psout.Diffuse.w = alpha;
#	endif

#	if defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 0) {
			psout.Diffuse.xyz = LightLimitFix::TurboColormap(LightLimitFix::NumStrictLights >= 7.0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			psout.Diffuse.xyz = LightLimitFix::TurboColormap((float)LightLimitFix::NumStrictLights / 15.0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 2) {
			psout.Diffuse.xyz = LightLimitFix::TurboColormap((float)numClusteredLights / MAX_CLUSTER_LIGHTS);
		} else {
			psout.Diffuse.xyz = shadowColor.xyz;
		}
		baseColor.xyz = 0.0;
	} else {
		psout.Diffuse.xyz = color.xyz;
	}
#	else
	psout.Diffuse.xyz = color.xyz;
#	endif  // defined(LIGHT_LIMIT_FIX)

	psout.MotionVectors.xy = screenMotionVector.xy;
	psout.MotionVectors.zw = float2(0, psout.Diffuse.w);

#	if defined(DEFERRED)

#		if defined(TERRAIN_BLENDING)
	psout.Diffuse.w = blendFactorTerrain;
#		endif

	psout.MotionVectors.zw = float2(0.0, psout.Diffuse.w);
	psout.Specular = float4(specularColor, psout.Diffuse.w);
	psout.Albedo = float4(outputAlbedo, psout.Diffuse.w);

	float outGlossiness = saturate(glossiness * SSRParams.w);

#		if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		outGlossiness = 1.0 - (SharedData::hairSpecularSettings.HairMode == 1 ? 1.0 : pow(abs(2.0 / (glossiness * 0.5 + 2.0)), 0.25));
	}
#		endif

#		if defined(WETNESS_EFFECTS)
	screenSpaceNormal = normalize(FrameBuffer::WorldToView(wetnessNormal, false, eyeIndex));
#		endif

#		if defined(TRUE_PBR)
	psout.Reflectance = float4(indirectSpecularLobeWeight, psout.Diffuse.w);
#			if defined(WETNESS_EFFECTS)
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), lerp(pbrGlossiness, max(pbrGlossiness, wetnessGlossinessSpecular), wetnessGlossinessSpecular), psout.Diffuse.w);
#			else
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), pbrGlossiness, psout.Diffuse.w);
#			endif
#		elif defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
#			if defined(WETNESS_EFFECTS)
		psout.Reflectance = float4(indirectSpecularLobeWeightPrim + indirectSpecularLobeWeightSec + wetnessReflectance, psout.Diffuse.w);
		psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), lerp(outGlossiness, max(outGlossiness, wetnessGlossinessSpecular), wetnessGlossinessSpecular), psout.Diffuse.w);
#			else
		psout.Reflectance = float4(indirectSpecularLobeWeightPrim + indirectSpecularLobeWeightSec, psout.Diffuse.w);
		psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), outGlossiness, psout.Diffuse.w);
#			endif
	} else {
#			if defined(WETNESS_EFFECTS)
		psout.Reflectance = float4(wetnessReflectance, psout.Diffuse.w);
		psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), lerp(outGlossiness, max(outGlossiness, wetnessGlossinessSpecular), wetnessGlossinessSpecular), psout.Diffuse.w);
#			else
		psout.Reflectance = float4(0.0.xxx, psout.Diffuse.w);
		psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), outGlossiness, psout.Diffuse.w);
#			endif
	}
#		elif defined(WETNESS_EFFECTS)
	psout.Reflectance = float4(wetnessReflectance, psout.Diffuse.w);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), lerp(outGlossiness, max(outGlossiness, wetnessGlossinessSpecular), wetnessGlossinessSpecular), psout.Diffuse.w);
#		else
	psout.Reflectance = float4(0.0.xxx, psout.Diffuse.w);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), outGlossiness, psout.Diffuse.w);
#		endif

#		if defined(SNOW)
#			if defined(TRUE_PBR)
	psout.Parameters.x = Color::RGBToLuminanceAlternative(specularColor);
	psout.Parameters.y = 0;
#			else
	psout.Parameters.x = Color::RGBToLuminanceAlternative(lightsSpecularColor);
#			endif
	psout.Parameters.w = psout.Diffuse.w;
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#			if defined(DYNAMIC_CUBEMAPS)
	if (dynamicCubemap) {
#				if defined(WETNESS_EFFECTS)
		psout.Reflectance.xyz = max(envColor, wetnessReflectance);
		psout.NormalGlossiness.z = lerp(1.0 - envRoughness, max(1.0 - envRoughness, wetnessGlossinessSpecular), wetnessGlossinessSpecular);
#				else
		psout.Reflectance.xyz = envColor;
		psout.NormalGlossiness.z = 1.0 - envRoughness;
#				endif
	}
#			endif
#		endif

#		if defined(SSS) && defined(SKIN)
	psout.Masks = float4(saturate(baseColor.a), !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsBeastRace), Color::RGBToYCoCg(directionalAmbientColor).x, psout.Diffuse.w);
#		else
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, psout.Diffuse.w);
#		endif

	float stochasticBlend = (screenNoise * screenNoise) < psout.Diffuse.w ? 1.0 : 0.0;
	psout.NormalGlossiness.w = stochasticBlend;
#	endif

	return psout;
}
#endif  // PSHADER
