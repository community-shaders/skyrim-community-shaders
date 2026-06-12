#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Skinned.hlsli"
#define EFFECT

#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
#if defined(TEXCOORD)
#	if defined(STRIP_PARTICLES)
	float3
#	else
	float2
#	endif
		TexCoord0: TEXCOORD0;
#endif
#if defined(NORMALS) || defined(MOTIONVECTORS_NORMALS)
	float4 Normal: NORMAL0;
#endif
#if defined(BINORMAL_TANGENT)
	float4 Bitangent: BINORMAL0;
#endif
#if defined(VC)
	float4 Color: COLOR0;
#endif
#if defined(SKINNED)
	float4 BoneWeights: BLENDWEIGHT0;
	float4 BoneIndices: BLENDINDICES0;
#endif
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
	float4 TexCoord0: TEXCOORD0;
	float4 WorldPosition: POSITION1;
#if defined(VC)
	float4 Color: COLOR0;
#endif
#if !defined(MOTIONVECTORS_NORMALS)
	float4 FogParam: COLOR1;
#endif
#if defined(MOTIONVECTORS_NORMALS) && defined(MEMBRANE) && !defined(SKINNED) && defined(NORMALS)
	float3 ScreenSpaceNormal: TEXCOORD1;
#elif (defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS))) || (defined(PROJECTED_UV) && defined(NORMALS))
	float3 TBN0: TEXCOORD1;
#endif
#if defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS))
	float FogAlpha: TEXCOORD5;
#endif
#if (defined(MEMBRANE) && defined(SKINNED) && !defined(NORMALS)) || (defined(PROJECTED_UV) && defined(NORMALS) && !defined(MEMBRANE))
	float3 TBN1: TEXCOORD2;
#endif
#if (defined(MEMBRANE) && defined(SKINNED) && !defined(NORMALS))
	float3 TBN2: TEXCOORD3;
#endif
#if defined(MEMBRANE)
	float4 ViewVector: TEXCOORD4;
#endif
#if defined(LIGHTING)
	float3 MSPosition: TEXCOORD6;
#endif
#if !(defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS)))
	float FogAlpha: TEXCOORD5;
#endif
#if defined(MOTIONVECTORS_NORMALS)
#	if !defined(LIGHTING) && !(defined(MEMBRANE) && defined(SKINNED)) && !(defined(MEMBRANE) && !defined(SKINNED) && defined(NORMALS))
	float3 ScreenSpaceNormal: TEXCOORD7;
#	endif
	float4 PreviousWorldPosition: POSITION2;
#	if (defined(LIGHTING) || (defined(MEMBRANE) && defined(SKINNED))) && !(defined(MEMBRANE) && defined(NORMALS))
	float3 ScreenSpaceNormal: TEXCOORD7;
#	endif
#endif
};

#ifdef VSHADER
cbuffer VS_PerFrame : register(b12)
{
	row_major float4x4 ScreenProj : packoffset(c0);
	row_major float4x4 ViewProj : packoffset(c8);
#	if defined(SKINNED)
	float3 BonesPivot : packoffset(c40);
#		if defined(MOTIONVECTORS_NORMALS)
	float3 PreviousBonesPivot : packoffset(c41);
#		endif  // MOTIONVECTORS_NORMALS
#	endif      // SKINNED
};

cbuffer PerTechnique : register(b0)
{
	float4 FogParam : packoffset(c0);
	float4 FogNearColor : packoffset(c1);
	float4 FogFarColor : packoffset(c2);
};

cbuffer PerMaterial : register(b1)
{
	float4 TexcoordOffset : packoffset(c0);
	float4 SoftMateralVSParams : packoffset(c1);
	float4 FalloffData : packoffset(c2);
};

cbuffer PerGeometry : register(b2)
{
	row_major float3x4 World : packoffset(c0);
	row_major float3x4 PreviousWorld : packoffset(c3);
	float4 MatProj[3] : packoffset(c6);
	float4 EyePosition : packoffset(c12);
	float4 PosAdjust : packoffset(c13);
	float4 TexcoordOffsetMembrane : packoffset(c14);
}

cbuffer IndexedTexcoordBuffer : register(b11)
{
	float4 IndexedTexCoord[128] : packoffset(c0);
}

#	if defined(PROJECTED_UV)
float GetProjectedU(float3 worldPosition, float4 texCoordOffset)
{
	float projUvTmp = min(abs(worldPosition.x), abs(worldPosition.y)) *
	                  (1 / max(abs(worldPosition.x), abs(worldPosition.y)));
	float projUvTmpSqr = projUvTmp * projUvTmp;
	float projUvTmp2 =
		projUvTmpSqr *
			(projUvTmpSqr *
					(projUvTmpSqr * (projUvTmpSqr * 0.0208350997 + -0.0851330012) + 0.180141002) +
				-0.330299497) +
		0.999866009;
	float projUvTmp5;
	if (abs(worldPosition.x) > abs(worldPosition.y)) {
		projUvTmp5 = projUvTmp * projUvTmp2 * -2 + Math::HALF_PI;
	} else {
		projUvTmp5 = 0;
	}
	float projUvTmp6 = projUvTmp * projUvTmp2 + projUvTmp5;
	float projUvTmp7;
	if (worldPosition.y < -worldPosition.y) {
		projUvTmp7 = -Math::PI;
	} else {
		projUvTmp7 = 0;
	}
	float projUvTmp4 = projUvTmp6 + projUvTmp7;
	float minCoord = min(worldPosition.x, worldPosition.y);
	float maxCoord = max(worldPosition.x, worldPosition.y);
	if (minCoord < -minCoord && maxCoord >= -maxCoord) {
		projUvTmp4 = -projUvTmp4;
	}
	return abs(0.318309158 * projUvTmp4) * texCoordOffset.w + texCoordOffset.y;
}

float GetProjectedV(float3 worldPosition)
{
	return (-PosAdjust.x + (PosAdjust.z + worldPosition.z)) / PosAdjust.y;
}
#	endif

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	precise float4 inputPosition = float4(input.Position.xyz, 1.0);

	precise row_major float4x4 world4x4 = float4x4(World[0], World[1], World[2], float4(0, 0, 0, 1));
	precise float3x3 world3x3 =
		transpose(float3x3(transpose(World)[0], transpose(World)[1], transpose(World)[2]));

#	if defined(SKY_OBJECT)
	float4x4 viewProj = float4x4(ViewProj[0], ViewProj[1], ViewProj[3], ViewProj[3]);
#	else
	row_major float4x4 viewProj = ViewProj;
#	endif

#	if defined(SKINNED)
	precise int4 actualIndices = 765.01.xxxx * input.BoneIndices.xyzw;
#		if defined(MOTIONVECTORS_NORMALS)
	float3x4 previousBoneTransformMatrix =
		Skinned::GetBoneTransformMatrix(PreviousBones, actualIndices, PreviousBonesPivot, input.BoneWeights);
	precise float4 previousWorldPosition =
		float4(mul(inputPosition, transpose(previousBoneTransformMatrix)), 1);
#		endif
	float3x4 boneTransformMatrix =
		Skinned::GetBoneTransformMatrix(Bones, actualIndices, BonesPivot, input.BoneWeights);
	precise float4 worldPosition = float4(mul(inputPosition, transpose(boneTransformMatrix)), 1);
	float4 viewPos = mul(viewProj, worldPosition);
#	else
	precise float4 worldPosition = float4(mul(World, inputPosition), 1);
	precise float4 previousWorldPosition = float4(mul(PreviousWorld, inputPosition), 1);
	precise row_major float4x4 modelView = mul(viewProj, world4x4);
	float4 viewPos = mul(modelView, inputPosition);
#	endif

	vsout.Position = viewPos;

#	if defined(SKINNED)
	float3x3 boneRSMatrix = Skinned::GetBoneRSMatrix(Bones, actualIndices, input.BoneWeights);
	float3x3 boneRSMatrixTr = transpose(boneRSMatrix);
#	endif

#	if defined(NORMALS) || defined(MOTIONVECTORS_NORMALS)
	float3 normal = input.Normal.xyz * 2 - 1;

#		if defined(SKINNED)
	float3 worldNormal = normalize(mul(normal, boneRSMatrixTr));
#		else
	float3 worldNormal = normalize(mul(world3x3, normal));
#		endif
#	endif

#	if defined(VC)
	vsout.Color = input.Color;
#	endif

#	if !defined(MOTIONVECTORS_NORMALS)
	float fogColorParam = min(FogParam.w,
		exp2(FogParam.z * log2(saturate(length(viewPos.xyz) * FogParam.y - FogParam.x))));

	vsout.FogParam.xyz = lerp(FogNearColor.xyz, FogFarColor.xyz, fogColorParam);
	vsout.FogParam.w = fogColorParam;
#	endif

	float4 texCoord = float4(0, 0, 1, 0);

#	if defined(MEMBRANE)
	float4 texCoordOffset = TexcoordOffsetMembrane;
#	else
	float4 texCoordOffset = TexcoordOffset;
#	endif

#	if defined(TEXCOORD_INDEX)
#		if defined(NORMALS)
	uint index = input.TexCoord0.z;
#		else
	uint index = input.Position.w;
#		endif
#	endif

#	if defined(PROJECTED_UV)
#		if defined(NORMALS) && !defined(MEMBRANE)
	texCoord.x = dot(MatProj[0].xyz, inputPosition.xyz);
#		else
	texCoord.x = GetProjectedU(worldPosition.xyz, texCoordOffset);
#		endif
#	else
#		if defined(TEXTURE)
	float u = input.TexCoord0.x;
#			if defined(TEXCOORD_INDEX)
	u = IndexedTexCoord[index].y * u + IndexedTexCoord[index].x;
#			endif
	texCoord.x = u * texCoordOffset.z + texCoordOffset.x;
#		endif
#	endif
#	if defined(PROJECTED_UV)
#		if defined(NORMALS) && !defined(MEMBRANE)
	texCoord.y = dot(MatProj[1].xyz, inputPosition.xyz);
#		else
	texCoord.y = GetProjectedV(worldPosition.xyz);
#		endif
#	else
#		if defined(TEXTURE)
	float v = input.TexCoord0.y;
#			if defined(TEXCOORD_INDEX)
	v = IndexedTexCoord[index].w * v + IndexedTexCoord[index].z;
#			endif
	texCoord.y = v * texCoordOffset.w + texCoordOffset.y;
#		endif
#	endif
#	if defined(PROJECTED_UV) && !defined(NORMALS)
	texCoord.w = input.TexCoord0.y;
#	elif defined(SOFT)
	texCoord.w = viewPos.w / SoftMateralVSParams.x;
#	elif defined(MEMBRANE) && (!defined(NORMALS) || defined(ALPHA_TEST))
	texCoord.w = input.TexCoord0.y;
#	endif
#	if defined(PROJECTED_UV) && !defined(NORMALS)
	texCoord.z = input.TexCoord0.x;
#	elif defined(FALLOFF)
	float3 inverseWorldDirection = normalize(-worldPosition.xyz);
	float WdotN = dot(worldNormal, inverseWorldDirection);
	float falloff = saturate((-FalloffData.x + abs(WdotN)) / (FalloffData.y - FalloffData.x));
	float falloffParam = (falloff * falloff) * (3 - falloff * 2);
	texCoord.z = lerp(FalloffData.z, FalloffData.w, falloffParam);
#	elif defined(MEMBRANE) && (!defined(NORMALS) || defined(ALPHA_TEST))
	texCoord.z = input.TexCoord0.x;
#	endif
	vsout.TexCoord0 = texCoord;

	float3 eyePosition = 0.0.xxx;
#	if defined(MEMBRANE) && defined(TEXTURE) && !defined(SKINNED)
	eyePosition = EyePosition.xyz;
#	endif

	float3 viewPosition = inputPosition.xyz;
#	if defined(SKINNED)
	viewPosition = worldPosition.xyz;
#	endif

#	if defined(MEMBRANE)
#		if defined(SKINNED)
#			if defined(NORMALS)
	vsout.TBN0.xyz = worldNormal;
#			else
	float3x3 tbnTr = float3x3(normalize(boneRSMatrixTr[0]), normalize(boneRSMatrixTr[1]),
		normalize(boneRSMatrixTr[2]));
#				if defined(MOTIONVECTORS_NORMALS)
	tbnTr[2] = worldNormal;
#				endif
	float3x3 tbn = transpose(tbnTr);
	vsout.TBN0.xyz = tbn[0];
	vsout.TBN1.xyz = tbn[1];
	vsout.TBN2.xyz = tbn[2];
#			endif
#		endif

	vsout.ViewVector.xyz = normalize(eyePosition - viewPosition);
	vsout.ViewVector.w = 1;
#	endif

#	if !defined(SKINNED) && defined(NORMALS) && !(defined(MOTIONVECTORS_NORMALS) && defined(MEMBRANE) && !defined(SKINNED) && defined(NORMALS))
#		if defined(MEMBRANE)
	vsout.TBN0.xyz = normal;
#		elif defined(PROJECTED_UV)
	vsout.TBN0.xyz = input.Normal.xyz;
#		endif
#	endif

#	if defined(MOTIONVECTORS_NORMALS) && !(defined(MEMBRANE) && defined(SKINNED) && defined(NORMALS))
#		if defined(SKINNED) && !defined(MEMBRANE)
	float3 screenSpaceNormal = normal;
#		elif defined(FALLOFF) || (defined(SKINNED) && defined(MEMBRANE))
	float3 screenSpaceNormal = worldNormal;
#		else
	float4x4 modelScreen = mul(ScreenProj, world4x4);
	float3 screenSpaceNormal = normalize(mul(modelScreen, float4(normal, 0))).xyz;
#		endif

	vsout.ScreenSpaceNormal = screenSpaceNormal;
#	endif

#	if defined(LIGHTING)
	vsout.MSPosition = viewPosition;
#	endif

	vsout.FogAlpha.x = FogNearColor.w;

#	if defined(PROJECTED_UV) && defined(NORMALS) && !defined(MEMBRANE)
	vsout.TBN1.xyz = MatProj[2].xyz;
#	endif

	vsout.WorldPosition = worldPosition;
#	if defined(MOTIONVECTORS_NORMALS)
	vsout.PreviousWorldPosition = previousWorldPosition;
#	endif

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;
SamplerState SampBaseSampler : register(s0);
SamplerState SampNormalSampler : register(s1);
SamplerState SampNoiseSampler : register(s2);
SamplerState SampDepthSampler : register(s3);
SamplerState SampGrayscaleSampler : register(s4);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexNormalSampler : register(t1);
Texture2D<float4> TexNoiseSampler : register(t2);
Texture2D<float4> TexDepthSamplerEffect : register(t3);
Texture2D<float4> TexGrayscaleSampler : register(t4);

#if defined(DEFERRED)
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
#	if defined(MOTIONVECTORS_NORMALS)
	float4 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
#	elif defined(NORMALS)
	float4 NormalGlossiness: SV_Target2;
#	endif
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
	float4 Reflectance: SV_Target5;
	float4 Masks: SV_Target6;
};
#else
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
#	if defined(MOTIONVECTORS_NORMALS)
	float2 MotionVectors: SV_Target1;
	float4 ScreenSpaceNormals: SV_Target2;
#	else
	float4 Color2: SV_Target2;
#	endif
};
#endif

#ifdef PSHADER

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

cbuffer PerTechnique : register(b0)
{
	float4 CameraDataEffect : packoffset(c0);
	float2 VPOSOffset : packoffset(c1);
	float2 FilteringParam : packoffset(c1.z);
};

cbuffer PerMaterial : register(b1)
{
	float4 BaseColor : packoffset(c0);
	float4 BaseColorScale : packoffset(c1);
	float4 LightingInfluence : packoffset(c2);
};

cbuffer PerGeometry : register(b2)
{
	float4 PLightPositionX[1] : packoffset(c0);
	float4 PLightPositionY[1] : packoffset(c1);
	float4 PLightPositionZ[1] : packoffset(c2);
	float4 PLightingRadiusInverseSquared : packoffset(c3);
	float4 PLightColorR : packoffset(c4);
	float4 PLightColorG : packoffset(c5);
	float4 PLightColorB : packoffset(c6);
	float4 DLightColor : packoffset(c7);
	float4 PropertyColor : packoffset(c8);
	float4 AlphaTestRef : packoffset(c9);
	float4 MembraneRimColor : packoffset(c10);
	float4 MembraneVars : packoffset(c11);
};

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#		include "InverseSquareLighting/InverseSquareLighting.hlsli"
#	endif

#	define LinearSampler SampBaseSampler

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(EXP_HEIGHT_FOG)
#		define SampColorSampler SampBaseSampler
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

#	include "Common/ShadowSampling.hlsli"

float3 GetEffectAmbientLighting(float skylightingDiffuse)
{
	float3 ambientColor = ShadowSampling::GetRawAmbientLighting(ShadowSampling::LightingSampleNormal);

#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
#		if defined(SKYLIGHTING)
		ambientColor = ImageBasedLighting::GetDiffuseIBLOccluded(ambientColor, ShadowSampling::ImageBasedLightingNormal, skylightingDiffuse);
#		else
		ambientColor = ImageBasedLighting::GetDiffuseIBL(ambientColor, ShadowSampling::ImageBasedLightingNormal);
#		endif
	}
#	endif

	return ambientColor;
}

void ExtractEffectLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor, float skylightingDiffuse)
{
	float3 ambientColorAmb = GetEffectAmbientLighting(skylightingDiffuse);
	float3 dirLightColorDir = ShadowSampling::GetDirectionalLighting();

	float inputLuma = Color::RGBToLuminance(inputColor);
	float ambientLuma = Color::RGBToLuminance(ambientColorAmb);
	float dirLightLuma = Color::RGBToLuminance(dirLightColorDir);

	float totalLuma = ambientLuma + dirLightLuma;

	if (totalLuma > 0.0 && ambientLuma > 0.0)
		ambientColorAmb *= inputLuma / totalLuma;

	float3 dirLightColorAmb = max(0.0, inputColor - ambientColorAmb);

	dirColor = dirLightColorAmb;
	ambientColor = ambientColorAmb;
}

#	if defined(LIGHTING)
float3 GetLightingColor(float3 msPosition, float3 worldPosition, float2 screenPosition, inout float shadowVariance)
{
	float3 color = DLightColor.xyz * Color::EffectLightingMult();
	bool suppressExternalEmittance = SharedData::InInterior && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::SuppressExternalEmittance);
	if (suppressExternalEmittance) {
		color = GetEffectAmbientLighting(1.0) + ShadowSampling::GetDirectionalLighting();
	}

#		if defined(SKYLIGHTING)
	float skylightingDiffuse = 1.0;
	if (!SharedData::InInterior) {
		float3 positionMSSkylight = worldPosition;

		sh2 skylightingSH = Skylighting::SampleNoBias(positionMSSkylight);
		skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, float3(0, 0, 1), Skylighting::GetFadeOutFactor(positionMSSkylight));
	}
#		endif

	float3 dirColor;
	float3 ambientColor;
#		if defined(SKYLIGHTING)
	ExtractEffectLighting(color, dirColor, ambientColor, skylightingDiffuse);
#		else
	ExtractEffectLighting(color, dirColor, ambientColor, 1.0);
#		endif

	float3 viewDirection = normalize(worldPosition.xyz);

	float unusedSurfaceShadow;
	float dirShadow = 1.0;

	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);

	if (inWorld && ShadowSampling::HasDirectionalShadows())
		dirShadow = ShadowSampling::Get3DFilteredShadow(worldPosition.xyz, viewDirection, screenPosition, unusedSurfaceShadow);

	shadowVariance = 1.0 - sqrt(saturate(fwidth(dirShadow)));

	dirColor *= dirShadow;

#		if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		dirColor *= ExponentialHeightFog::GetSunlightFogAttenuation(worldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#		endif

#		if defined(SKYLIGHTING)
#			if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#			endif
	{
		ambientColor = Color::IrradianceToLinear(ambientColor);
		ambientColor *= skylightingDiffuse;
		ambientColor = Color::IrradianceToGamma(ambientColor);
	}
#		endif

	color = dirColor + ambientColor;

#		if defined(LIGHT_LIMIT_FIX)
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld))
#		endif
	{
		float4 lightDistanceSquared = (PLightPositionX[0] - msPosition.xxxx) * (PLightPositionX[0] - msPosition.xxxx) + (PLightPositionY[0] - msPosition.yyyy) * (PLightPositionY[0] - msPosition.yyyy) + (PLightPositionZ[0] - msPosition.zzzz) * (PLightPositionZ[0] - msPosition.zzzz);
		float4 lightFadeMul = 1.0.xxxx - saturate(PLightingRadiusInverseSquared * lightDistanceSquared);
		color.x += dot(Color::PointLight(PLightColorR.xxx).x * lightFadeMul * Color::EffectLightingMult(), 1.0.xxxx);
		color.y += dot(Color::PointLight(PLightColorG.xxx).x * lightFadeMul * Color::EffectLightingMult(), 1.0.xxxx);
		color.z += dot(Color::PointLight(PLightColorB.xxx).x * lightFadeMul * Color::EffectLightingMult(), 1.0.xxxx);
	}

	return color;
}
#	else
float3 GetLightingShadow(float3 color, float3 worldPosition, float2 screenPosition, float depth, inout float shadowVariance)
{
	float3 dirColor;
	float3 ambientColor;
	float skylightingDiffuse = 1.0;
#		if defined(SKYLIGHTING)
	ExtractEffectLighting(color, dirColor, ambientColor, skylightingDiffuse);
#		else
	ExtractEffectLighting(color, dirColor, ambientColor, 1.0);
#		endif

	static const uint sampleCount = 8;
	static const float rcpSampleCount = 1.0 / float(sampleCount);

	float noise = Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount);
	float noiseTransform = noise * 2.0 - 1.0;
	float2 rotation;
	sincos(Math::TAU * noise, rotation.y, rotation.x);
	float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

	// Enough for sky statics
	float maxDistance = max(0, SharedData::GetScreenDepth(depth));
	float viewRayLength = 2048.0;
	float3 viewDirection = normalize(worldPosition);
	float3 startPosition = worldPosition - viewDirection * viewRayLength;
	float3 endPosition = worldPosition + viewDirection * min(maxDistance, viewRayLength);

	float shadow = 1.0;

	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);

	if (inWorld && !SharedData::InInterior) {
		shadow = 0.0;
		for (uint i = 0; i < sampleCount; i++) {
			float t = (float(i) + noise) * rcpSampleCount;
			float3 samplePositionWS = lerp(startPosition, endPosition, t);
			shadow += ShadowSampling::GetWorldShadow(samplePositionWS, FrameBuffer::CameraPosAdjust.xyz);
		}
		shadow *= rcpSampleCount;
	}

	shadowVariance = 1.0 - sqrt(saturate(fwidth(shadow)));

	dirColor *= shadow;

#		if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		dirColor *= ExponentialHeightFog::GetSunlightFogAttenuation(worldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#		endif

	return dirColor + ambientColor;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout = (PS_OUTPUT)0;

	float4 fogMul = 1;
#	if !defined(MULTBLEND)
	fogMul.xyz = input.FogAlpha;
#	endif

#	if defined(MEMBRANE)
#		if !defined(MOTIONVECTORS_NORMALS) && defined(ALPHA_TEST)
	float noiseAlpha = TexNoiseSampler.Sample(SampNoiseSampler, input.TexCoord0.zw).w;
#			if defined(VC)
	noiseAlpha *= input.Color.w;
#			endif
	if (noiseAlpha - AlphaTestRef.x < 0) {
		discard;
	}
#		endif

#		if defined(MOTIONVECTORS_NORMALS) && defined(MEMBRANE) && !defined(SKINNED) && defined(NORMALS)
	float3 normal = input.ScreenSpaceNormal;
#		elif defined(NORMALS)
	float3 normal = input.TBN0;
#		else
	float3 normal = TexNormalSampler.Sample(SampNormalSampler, input.TexCoord0.zw).xzy * 2 - 1;
#			if defined(SKINNED)
	normal = mul(normal, transpose(float3x3(input.TBN0, input.TBN1, input.TBN2)));
#			endif
#		endif
	float NdotV = dot(normal, input.ViewVector.xyz);
	float membraneColorMul = pow(saturate(1 - NdotV), MembraneVars.x);
	float4 membraneColor = MembraneRimColor * membraneColorMul;
#	elif defined(PROJECTED_UV) && defined(NORMALS)
	float2 noiseTexCoord = 0.00333333341 * input.TexCoord0.xy;
	float noise = TexNoiseSampler.Sample(SampNoiseSampler, noiseTexCoord).x * 0.2 + 0.4;
	if (dot(input.TBN0, input.TBN1) - noise < 0) {
		discard;
	}
#	endif

	float softMul = 1;
	float depth = 1;
#	if defined(SOFT)
	depth = TexDepthSamplerEffect.Load(int3(input.Position.xy, 0)).x;
	softMul = saturate(-input.TexCoord0.w + LightingInfluence.y / ((1 - depth) * CameraDataEffect.z + CameraDataEffect.y));
#	endif

	float lightingInfluence = LightingInfluence.x;
	float3 propertyColor = Color::Effect(PropertyColor.xyz);
	float shadowVariance = 1.0;

#	if defined(LIGHTING)
	propertyColor = GetLightingColor(input.MSPosition.xyz, input.WorldPosition.xyz, input.Position.xy, shadowVariance);

#		if defined(LIGHT_LIMIT_FIX)
	uint lightCount = 0;

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	bool inWorld = Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld;

	uint clusterIndex = 0;
	if (inWorld && LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
			LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];
			if (LightLimitFix::IsLightIgnored(light) || light.lightFlags & LightLimitFix::LightFlags::Shadow) {
				continue;
			}
			float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
			float lightDist = length(lightDirection);

#			if defined(ISL)
			float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
#			else
			float intensityFactor = saturate(lightDist / light.radius);
			float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#			endif

			const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
			float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * intensityMultiplier * 0.5 * light.fade * Color::EffectLightingMult();
			propertyColor += lightColor;
		}
	}

#		endif
#	elif defined(MEMBRANE)
	propertyColor *= 0;
	lightingInfluence = 0;
#	endif

	float4 baseTexColor = float4(1, 1, 1, 1);
	float4 baseColor = float4(1, 1, 1, 1);
#	if !defined(TEXTURE)
	[branch] if (Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToColor || Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToAlpha)
#	endif
	{
		baseTexColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
		baseTexColor.xyz = Color::Effect(baseTexColor.xyz);
		baseColor *= baseTexColor;
		if (Permutation::PixelShaderDescriptor & Permutation::EffectFlags::IgnoreTexAlpha || Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToAlpha) {
			baseColor.w = 1;
		}
	}

#	if defined(MEMBRANE)
	float4 baseColorMul = float4(1, 1, 1, 1);
#	else
	float4 baseColorMul = BaseColor;
	baseColorMul.xyz = Color::Effect(baseColorMul.xyz);
#		if defined(VC) && !defined(PROJECTED_UV)
	baseColorMul *= float4(Color::Effect(input.Color.xyz), input.Color.w);
#		endif
#	endif

#	if defined(MEMBRANE)
	baseColor.w *= input.ViewVector.w;
#	else
	baseColor.w *= input.TexCoord0.z;
#	endif

	baseColor = baseColorMul * baseColor;
	baseColor.w *= softMul;

#	if defined(SOFT) && !(defined(FALLOFF) && defined(MULTBLEND))
	if (baseColor.w - 0.003 < 0) {
		discard;
	}
#	endif

	float alpha = baseColor.w;

#	if defined(BLOOD)
	alpha = baseColor.y;
	float deltaY = saturate(baseColor.y - AlphaTestRef.x);
	float bloodMul = baseColor.z;
#		if defined(VC)
	bloodMul *= input.Color.w;
#		endif
	if (deltaY < AlphaTestRef.y) {
		bloodMul *= (deltaY / AlphaTestRef.y);
	}
	baseColor.xyz = saturate(float3(2, 1, 1) - bloodMul.xxx) * (-bloodMul * AlphaTestRef.z + 1);
#	endif

	alpha *= PropertyColor.w;

	float baseColorScale = BaseColorScale.x;

#	if defined(MEMBRANE)
	baseColor.xyz = (PropertyColor.xyz + baseColor.xyz) * alpha + membraneColor.xyz * membraneColor.w;
	alpha += membraneColor.w;
	baseColorScale = MembraneVars.z;
#	endif

	if (Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToAlpha)
		alpha = TexGrayscaleSampler.Sample(SampGrayscaleSampler, float2(baseTexColor.w, alpha)).w;

	[branch] if (Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToColor)
	{
		float2 grayscaleToColorUv = float2(baseTexColor.y, baseColorMul.x);
#	if defined(MEMBRANE)
		grayscaleToColorUv.y = PropertyColor.x;
#	endif
		baseColor.xyz = Color::Effect(baseColorScale * TexGrayscaleSampler.Sample(SampGrayscaleSampler, grayscaleToColorUv).xyz);
	}

	float3 lightColor = lerp(baseColor.xyz, propertyColor * baseColor.xyz, lightingInfluence);

#	if !defined(MOTIONVECTORS_NORMALS)
	if (alpha * fogMul.w - AlphaTestRefRS < 0) {
		discard;
	}
#	endif

#	if !defined(LIGHTING) && defined(VC) && defined(TEXCOORD) && defined(NORMALS) && defined(TEXTURE) && defined(FALLOFF) && defined(SOFT)
	if (Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToAlpha && lightingInfluence == 1.0)
		lightColor = GetLightingShadow(lightColor, input.WorldPosition.xyz, input.Position.xy, depth, shadowVariance);
#	endif

	lightColor = Color::EffectMult(lightColor);

#	if !defined(MOTIONVECTORS_NORMALS)
	float fogFactor = Color::FogAlpha(input.FogParam.w);
	float3 fogColor = Color::Fog(input.FogParam.xyz);
#		if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		fogColor = ImageBasedLighting::GetFogIBLColor(fogColor);
	}
#		endif
#		if defined(EXP_HEIGHT_FOG)
	float vanillaFogFactor = fogFactor;
	float3 vanillaFogColor = fogColor;
	float expFogFactor = 0;
	if (SharedData::exponentialHeightFogSettings.enabled) {
		float4 exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFog(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		expFogFactor = exponentialHeightFog.w;
#			if defined(ADDBLEND) || defined(MULTBLEND) || defined(MULTBLEND_DECAL)
		fogColor = exponentialHeightFog.xyz;
		fogFactor = exponentialHeightFog.w;
#			else
		fogColor = exponentialHeightFog.xyz;
		fogFactor = exponentialHeightFog.w;
		alpha *= 1 - exponentialHeightFog.w;
#			endif
		if (ExponentialHeightFog::ShouldDisableVanillaFog()) {
			vanillaFogColor = lightColor;
			vanillaFogFactor = 0;
		}
	}
#		endif
#		if defined(ADDBLEND)
#			if defined(EXP_HEIGHT_FOG)
	float3 blendedColor = lightColor * (1 - vanillaFogFactor) * (1 - expFogFactor);
#			else
	float3 blendedColor = lightColor * (1 - fogFactor);
#			endif
#		elif defined(MULTBLEND) || defined(MULTBLEND_DECAL)
#			if defined(EXP_HEIGHT_FOG)
	float3 blendedColor = lerp(lightColor, 1.0.xxx, saturate(1.5 * vanillaFogFactor).xxx);
	blendedColor = lerp(blendedColor, 1.0.xxx, saturate(1.5 * expFogFactor).xxx);
#			else
	float3 blendedColor = lerp(lightColor, 1.0.xxx, saturate(1.5 * fogFactor).xxx);
#			endif
#		else
#			if defined(EXP_HEIGHT_FOG)
	float3 blendedColor = lerp(lightColor, vanillaFogColor, vanillaFogFactor.xxx);
	blendedColor = lerp(blendedColor, fogColor, fogFactor.xxx);
#			else
	float3 blendedColor = lerp(lightColor, fogColor, fogFactor.xxx);
#			endif
#		endif
#	else
	float3 blendedColor = lightColor.xyz;
#	endif

	alpha = Color::EffectAlpha(alpha);

	float4 finalColor = float4(blendedColor, alpha);
#	if defined(MULTBLEND_DECAL)
	finalColor.xyz *= alpha;
#	else
	finalColor *= fogMul;
#	endif
	psout.Diffuse = finalColor;
#	if defined(LIGHTING) && defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 0) {
			psout.Diffuse.xyz = Color::TurboColormap(0.0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			psout.Diffuse.xyz = Color::TurboColormap(0.0);
		} else {
			psout.Diffuse.xyz = Color::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
		}
	}
#	endif

#	if defined(DEFERRED)

#		if defined(MOTIONVECTORS_NORMALS)
#			if (defined(MEMBRANE) && defined(SKINNED) && defined(NORMALS))
	float3 screenSpaceNormal = normalize(input.TBN0);
#			else
	float3 screenSpaceNormal = normalize(input.ScreenSpaceNormal);
#			endif
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), 0.0, psout.Diffuse.w);
	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
	psout.MotionVectors = float4(screenMotionVector, 0.0, psout.Diffuse.w);
#		endif

#		if defined(MULTBLEND) || defined(MULTBLEND_DECAL)
	psout.Specular = float4(psout.Diffuse.xyz, finalColor.w);
	psout.Albedo = float4(psout.Diffuse.xyz, finalColor.w);
	psout.Reflectance = float4(psout.Diffuse.xyz, finalColor.w);
	psout.Masks = float4(Color::RGBToLuminance(psout.Diffuse.xyz).xxx, finalColor.w);
#		else
	psout.Albedo = float4(0, 0, 0, finalColor.w);
	psout.Specular = float4(0, 0, 0, finalColor.w);
	psout.Reflectance = float4(0, 0, 0, finalColor.w);
	psout.Masks = float4(0, 0, 0, finalColor.w);
#		endif

#	elif defined(MOTIONVECTORS_NORMALS)
	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
	psout.MotionVectors = screenMotionVector;

#		if (defined(MEMBRANE) && defined(SKINNED) && defined(NORMALS))
	float3 screenSpaceNormal = normalize(input.TBN0);
#		else
	float3 screenSpaceNormal = normalize(input.ScreenSpaceNormal);
#		endif

	screenSpaceNormal.z = max(0.001, sqrt(8 + -8 * screenSpaceNormal.z));
	screenSpaceNormal.xy /= screenSpaceNormal.zz;
	psout.ScreenSpaceNormals.xy = screenSpaceNormal.xy + 0.5.xx;
	psout.ScreenSpaceNormals.zw = 0.0.xx;
#	else
	psout.Color2 = finalColor;
#	endif

#	if !defined(HDR_OUTPUT)
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld) && SharedData::linearLightingSettings.enableLinearLighting) {
		psout.Diffuse.xyz = Color::LinearToSrgb(psout.Diffuse.xyz);
	}
#	endif
	return psout;
}
#endif
