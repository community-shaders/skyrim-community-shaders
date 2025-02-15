#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Skinned.hlsli"
#include "Common/VR.hlsli"

#define EFFECT

struct VS_INPUT
{
	float4 Position : POSITION0;
#if defined(TEXCOORD)
#	if defined(STRIP_PARTICLES)
	float3
#	else
	float2
#	endif
		TexCoord0 : TEXCOORD0;
#endif
#if defined(NORMALS) || defined(MOTIONVECTORS_NORMALS)
	float4 Normal : NORMAL0;
#endif
#if defined(BINORMAL_TANGENT)
	float4 Bitangent : BINORMAL0;
#endif
#if defined(VC)
	float4 Color : COLOR0;
#endif
#if defined(SKINNED)
	float4 BoneWeights : BLENDWEIGHT0;
	float4 BoneIndices : BLENDINDICES0;
#endif
#if defined(VR)
	uint InstanceID : SV_INSTANCEID;
#endif  // VR
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION0;
	float4 TexCoord0 : TEXCOORD0;
	float4 WorldPosition : POSITION1;
#if defined(VC)
	float4 Color : COLOR0;
#endif
#if !defined(MOTIONVECTORS_NORMALS)
	float4 FogParam : COLOR1;
#endif
#if defined(MOTIONVECTORS_NORMALS) && defined(MEMBRANE) && !defined(SKINNED) && defined(NORMALS)
	float3 ScreenSpaceNormal : TEXCOORD1;
#elif (defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS))) || (defined(PROJECTED_UV) && defined(NORMALS))
	float3 TBN0 : TEXCOORD1;
#endif
#if defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS))
	float FogAlpha : TEXCOORD5;
#endif
#if (defined(MEMBRANE) && defined(SKINNED) && !defined(NORMALS)) || (defined(PROJECTED_UV) && defined(NORMALS) && !defined(MEMBRANE))
	float3 TBN1 : TEXCOORD2;
#endif
#if (defined(MEMBRANE) && defined(SKINNED) && !defined(NORMALS))
	float3 TBN2 : TEXCOORD3;
#endif
#if defined(MEMBRANE)
	float4 ViewVector : TEXCOORD4;
#endif
#if defined(LIGHTING)
	float3 MSPosition : TEXCOORD6;
#endif
#if !(defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS)))
	float FogAlpha : TEXCOORD5;
#endif
#if defined(MOTIONVECTORS_NORMALS)
#	if !defined(LIGHTING) && !(defined(MEMBRANE) && defined(SKINNED)) && !(defined(MEMBRANE) && !defined(SKINNED) && defined(NORMALS))
	float3 ScreenSpaceNormal : TEXCOORD7;
#	endif
	float4 PreviousWorldPosition : POSITION2;
#	if (defined(LIGHTING) || (defined(MEMBRANE) && defined(SKINNED))) && !(defined(MEMBRANE) && defined(NORMALS))
	float3 ScreenSpaceNormal : TEXCOORD7;
#	endif
#endif
#if defined(VR)
	float ClipDistance : SV_ClipDistance0;  // o11
	float CullDistance : SV_CullDistance0;  // p11
	uint EyeIndex : EYEIDX0;
#endif  // VR
};

#ifdef VSHADER
cbuffer VS_PerFrame : register(b12)
{
#	if !defined(VR)
	row_major float4x4 ScreenProj[1] : packoffset(c0);
	row_major float4x4 ViewProj[1] : packoffset(c8);
#		if defined(SKINNED)
	float3 BonesPivot[1] : packoffset(c40);
#			if defined(MOTIONVECTORS_NORMALS)
	float3 PreviousBonesPivot[1] : packoffset(c41);
#			endif  // MOTIONVECTORS_NORMALS
#		endif      // SKINNED
#	else
	row_major float4x4 ScreenProj[2] : packoffset(c0);
	row_major float4x4 ViewProj[2] : packoffset(c16);
#		if defined(SKINNED)
	float3 BonesPivot[2] : packoffset(c80);
#			if defined(MOTIONVECTORS_NORMALS)
	float3 PreviousBonesPivot[2] : packoffset(c82);
#			endif  // MOTIONVECTORS_NORMALS
#		endif      // SKINNED
#	endif          // VR
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
#	if !defined(VR)
	row_major float3x4 World[1] : packoffset(c0);
	row_major float3x4 PreviousWorld[1] : packoffset(c3);
	float4 MatProj[3] : packoffset(c6);
	float4 EyePosition[1] : packoffset(c12);
	float4 PosAdjust[1] : packoffset(c13);
	float4 TexcoordOffsetMembrane : packoffset(c14);
#	else
	row_major float3x4 World[2] : packoffset(c0);
	row_major float3x4 PreviousWorld[2] : packoffset(c6);
	float4 MatProj[3] : packoffset(c12);
	float4 EyePosition[2] : packoffset(c21);
	float4 PosAdjust[2] : packoffset(c23);
	float4 TexcoordOffsetMembrane : packoffset(c25);
#	endif  // VR
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

float GetProjectedV(float3 worldPosition, uint a_eyeIndex = 0)
{
	return (-PosAdjust[a_eyeIndex].x + (PosAdjust[a_eyeIndex].z + worldPosition.z)) / PosAdjust[a_eyeIndex].y;
}
#	endif

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	uint eyeIndex = Stereo::GetEyeIndexVS(
#	if defined(VR)
		input.InstanceID
#	endif  // VR
	);
	precise float4 inputPosition = float4(input.Position.xyz, 1.0);

	precise row_major float4x4 world4x4 = float4x4(World[eyeIndex][0], World[eyeIndex][1], World[eyeIndex][2], float4(0, 0, 0, 1));
	precise float3x3 world3x3 =
		transpose(float3x3(transpose(World[eyeIndex])[0], transpose(World[eyeIndex])[1], transpose(World[eyeIndex])[2]));

#	if defined(SKY_OBJECT)
	float4x4 viewProj = float4x4(ViewProj[eyeIndex][0], ViewProj[eyeIndex][1], ViewProj[eyeIndex][3], ViewProj[eyeIndex][3]);
#	else
	row_major float4x4 viewProj = ViewProj[eyeIndex];
#	endif

#	if defined(SKINNED)
	precise int4 actualIndices = 765.01.xxxx * input.BoneIndices.xyzw;
#		if defined(MOTIONVECTORS_NORMALS)
	float3x4 previousBoneTransformMatrix =
		Skinned::GetBoneTransformMatrix(PreviousBones, actualIndices, PreviousBonesPivot[eyeIndex], input.BoneWeights);
	precise float4 previousWorldPosition =
		float4(mul(inputPosition, transpose(previousBoneTransformMatrix)), 1);
#		endif
	float3x4 boneTransformMatrix =
		Skinned::GetBoneTransformMatrix(Bones, actualIndices, BonesPivot[eyeIndex], input.BoneWeights);
	precise float4 worldPosition = float4(mul(inputPosition, transpose(boneTransformMatrix)), 1);
	float4 viewPos = mul(viewProj, worldPosition);
#	else
	precise float4 worldPosition = float4(mul(World[eyeIndex], inputPosition), 1);
	precise float4 previousWorldPosition = float4(mul(PreviousWorld[eyeIndex], inputPosition), 1);
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
	texCoord.y = GetProjectedV(worldPosition.xyz, eyeIndex);
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
	eyePosition = EyePosition[eyeIndex].xyz;
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
	float4x4 modelScreen = mul(ScreenProj[eyeIndex], world4x4);
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

#	ifdef VR
	vsout.EyeIndex = eyeIndex;
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(vsout.Position, eyeIndex);
	vsout.Position = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#	endif  // VR
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
	float4 Diffuse : SV_Target0;
#	if defined(MOTIONVECTORS_NORMALS)
	float4 MotionVectors : SV_Target1;
	float4 NormalGlossiness : SV_Target2;
#	elif defined(NORMALS)
	float4 NormalGlossiness : SV_Target2;
#	endif
	float4 Albedo : SV_Target3;
	float4 Specular : SV_Target4;
	float4 Reflectance : SV_Target5;
	float4 Masks : SV_Target6;
};
#else
struct PS_OUTPUT
{
	float4 Diffuse : SV_Target0;
#	if defined(MOTIONVECTORS_NORMALS)
	float2 MotionVectors : SV_Target1;
	float4 ScreenSpaceNormals : SV_Target2;
#	else
	float4 Normal : SV_Target1;
	float4 Color2 : SV_Target2;
#	endif
};
#endif

#ifdef PSHADER

#	if !defined(VR)
cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif  // !VR

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
#	if !defined(VR)
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
#	else
	float4 PLightPositionX[2] : packoffset(c0);
	float4 PLightPositionY[2] : packoffset(c2);
	float4 PLightPositionZ[2] : packoffset(c4);
	float4 PLightingRadiusInverseSquared : packoffset(c6);
	float4 PLightColorR : packoffset(c7);
	float4 PLightColorG : packoffset(c8);
	float4 PLightColorB : packoffset(c9);
	float4 DLightColor : packoffset(c10);
	float4 PropertyColor : packoffset(c11);  // VR should be 11; this could start earlier though
	float4 AlphaTestRef : packoffset(c12);
	float4 MembraneRimColor : packoffset(c13);
	float4 MembraneVars : packoffset(c14);
#	endif
};

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	define LinearSampler SampBaseSampler

#	if defined(TERRAIN_SHADOWS)
#		include "TerrainShadows/TerrainShadows.hlsli"
#	endif

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	include "Common/ShadowSampling.hlsli"

#	if defined(LIGHTING)
float3 GetLightingColor(float3 msPosition, float3 worldPosition, float4 screenPosition, uint eyeIndex)
{
	float4 lightDistanceSquared = (PLightPositionX[eyeIndex] - msPosition.xxxx) * (PLightPositionX[eyeIndex] - msPosition.xxxx) + (PLightPositionY[eyeIndex] - msPosition.yyyy) * (PLightPositionY[eyeIndex] - msPosition.yyyy) + (PLightPositionZ[eyeIndex] - msPosition.zzzz) * (PLightPositionZ[eyeIndex] - msPosition.zzzz);
	float4 lightFadeMul = 1.0.xxxx - saturate(PLightingRadiusInverseSquared * lightDistanceSquared);

	float3 color = DLightColor.xyz;

	if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::EffectShadows)) {
		float3 dirLightColor = SharedData::DirLightColor * 0.5;
		float3 ambientColor = mul(SharedData::DirectionalAmbient, float4(0, 0, 1, 1));

		color = ambientColor;

#		if defined(SKYLIGHTING)
#			if defined(VR)
		float3 positionMSSkylight = worldPosition + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#			else
		float3 positionMSSkylight = worldPosition;
#			endif

		sh2 skylightingSH = Skylighting::sampleNoBias(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, positionMSSkylight);
		float skylighting = SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1));
		skylighting = lerp(1.0, skylighting, Skylighting::getFadeOutFactor(worldPosition));
		color = Color::GammaToLinear(color);
		color *= Skylighting::mixDiffuse(SharedData::skylightingSettings, skylighting);
		color = Color::LinearToGamma(color);
#		endif

		if (!SharedData::InInterior)
			color += dirLightColor * ShadowSampling::GetEffectShadow(worldPosition, normalize(worldPosition), screenPosition, eyeIndex);
		else
			color += dirLightColor;
	} else {
#		if defined(SKYLIGHTING)
#			if defined(VR)
		float3 positionMSSkylight = worldPosition + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#			else
		float3 positionMSSkylight = worldPosition;
#			endif

		sh2 skylightingSH = Skylighting::sampleNoBias(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, positionMSSkylight);
		float skylighting = SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1));
		skylighting = lerp(1.0, skylighting, Skylighting::getFadeOutFactor(worldPosition));
		color = Color::GammaToLinear(color);
		color *= Skylighting::mixDiffuse(SharedData::skylightingSettings, skylighting);
		color = Color::LinearToGamma(color);
#		endif
	}

#		if defined(LIGHT_LIMIT_FIX)
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld))
#		endif
	{
		color.x += dot(PLightColorR * lightFadeMul, 1.0.xxxx);
		color.y += dot(PLightColorG * lightFadeMul, 1.0.xxxx);
		color.z += dot(PLightColorB * lightFadeMul, 1.0.xxxx);
	}

	return color;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if !defined(VR)
	uint eyeIndex = 0;
#	else
	uint eyeIndex = input.EyeIndex;
#	endif  // !VR

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
#	if defined(SOFT)
	float depth = TexDepthSamplerEffect.Load(int3(input.Position.xy, 0)).x;
	softMul = saturate(-input.TexCoord0.w + LightingInfluence.y / ((1 - depth) * CameraDataEffect.z + CameraDataEffect.y));
#	endif

	float lightingInfluence = LightingInfluence.x;
	float3 propertyColor = PropertyColor.xyz;

#	if defined(LIGHTING)
	propertyColor = GetLightingColor(input.MSPosition.xyz, input.WorldPosition.xyz, input.Position.xyzw, eyeIndex);

#		if defined(LIGHT_LIMIT_FIX)
	uint lightCount = 0;
	if (LightingInfluence.x > 0.0) {
		float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
		float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
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
				float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);
				float intensityFactor = saturate(lightDist / light.radius);
				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
				float3 lightColor = light.color.xyz * intensityMultiplier * 0.5;
				propertyColor += lightColor;
			}
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
		baseColor *= baseTexColor;
		if (Permutation::PixelShaderDescriptor & Permutation::EffectFlags::IgnoreTexAlpha || Permutation::PixelShaderDescriptor & Permutation::EffectFlags::GrayscaleToAlpha) {
			baseColor.w = 1;
		}
	}

#	if defined(MEMBRANE)
	float4 baseColorMul = float4(1, 1, 1, 1);
#	else
	float4 baseColorMul = BaseColor;
#		if defined(VC) && !defined(PROJECTED_UV)
	baseColorMul *= input.Color;
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
		baseColor.xyz = baseColorScale * TexGrayscaleSampler.Sample(SampGrayscaleSampler, grayscaleToColorUv).xyz;
	}

	float3 lightColor = lerp(baseColor.xyz, propertyColor * baseColor.xyz, lightingInfluence.xxx);

#	if !defined(MOTIONVECTORS_NORMALS)
	if (alpha * fogMul.w - AlphaTestRefRS < 0) {
		discard;
	}
#	endif

#	if !defined(MOTIONVECTORS_NORMALS)
#		if defined(ADDBLEND)
	float3 blendedColor = lightColor * (1 - input.FogParam.www);
#		elif defined(MULTBLEND) || defined(MULTBLEND_DECAL)
	float3 blendedColor = lerp(lightColor, 1.0.xxx, saturate(1.5 * input.FogParam.w).xxx);
#		else
	float3 blendedColor = lerp(lightColor, input.FogParam.xyz, input.FogParam.www);
#		endif
#	else
	float3 blendedColor = lightColor.xyz;
#	endif

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
			psout.Diffuse.xyz = LightLimitFix::TurboColormap(0.0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			psout.Diffuse.xyz = LightLimitFix::TurboColormap(0.0);
		} else {
			psout.Diffuse.xyz = LightLimitFix::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
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
	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);
	psout.MotionVectors = float4(screenMotionVector, 0.0, psout.Diffuse.w);
#		endif

	psout.Specular = float4(0.0.xxx, finalColor.w);
	psout.Albedo = float4(0.0.xxx, finalColor.w);
	psout.Reflectance = float4(0.0.xxx, finalColor.w);
	psout.Masks = float4(0.0.xxx, finalColor.w);

#	elif defined(MOTIONVECTORS_NORMALS)
	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);
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
	psout.Normal = float4(!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::EffectShadows), 0, 0, finalColor.w);
	psout.Color2 = finalColor;
#	endif

	return psout;
}
#endif