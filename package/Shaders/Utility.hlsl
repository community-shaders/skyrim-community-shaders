#include "Common/FrameBuffer.hlsli"
#include "Common/LodLandscape.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Skinned.hlsli"
#include "Common/VR.hlsli"

#if defined(RENDER_SHADOWMASK) || defined(RENDER_SHADOWMASKSPOT) || defined(RENDER_SHADOWMASKPB) || defined(RENDER_SHADOWMASKDPB)
#	define RENDER_SHADOWMASK_ANY
#endif

struct VS_INPUT
{
	float4 PositionMS : POSITION0;

#if defined(TEXTURE)
	float2 TexCoord : TEXCOORD0;
#endif

#if defined(NORMALS)
	float4 Normal : NORMAL0;
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
	float4 PositionCS : SV_POSITION0;

#if !(defined(RENDER_DEPTH) && defined(RENDER_SHADOWMASK_ANY)) && SHADOWFILTER != 2
#	if (defined(ALPHA_TEST) && ((!defined(RENDER_DEPTH) && !defined(RENDER_SHADOWMAP)) || defined(RENDER_SHADOWMAP_PB))) || defined(RENDER_NORMAL) || defined(DEBUG_SHADOWSPLIT) || defined(RENDER_BASE_TEXTURE)
	float4 TexCoord0 : TEXCOORD0;
#	endif

#	if defined(RENDER_NORMAL)
	float4 Normal : TEXCOORD1;
#	endif

#	if defined(RENDER_SHADOWMAP_PB)
	float3 TexCoord1 : TEXCOORD2;
#	elif defined(ALPHA_TEST) && (defined(RENDER_DEPTH) || defined(RENDER_SHADOWMAP))
	float4 TexCoord1 : TEXCOORD2;
#	elif defined(ADDITIONAL_ALPHA_MASK)
	float2 TexCoord1 : TEXCOORD2;
#	endif

#	if defined(LOCALMAP_FOGOFWAR)
	float Alpha : TEXCOORD3;
#	endif

#	if defined(RENDER_SHADOWMASK_ANY)
	float4 PositionMS : TEXCOORD5;
#	endif

#	if defined(ALPHA_TEST) && defined(VC) && defined(RENDER_SHADOWMASK_ANY)
	float2 Alpha : TEXCOORD4;
#	elif (defined(ALPHA_TEST) && defined(VC) && !defined(TREE_ANIM)) || defined(RENDER_SHADOWMASK_ANY)
	float Alpha : TEXCOORD4;
#	endif

#	if defined(DEBUG_SHADOWSPLIT)
	float Depth : TEXCOORD2;
#	endif
#endif
#if defined(VR)
	float ClipDistance : SV_ClipDistance0;  // o11
	float CullDistance : SV_CullDistance0;  // p11
	uint EyeIndex : EYEIDX0;
#endif  // VR
};

#ifdef VSHADER
cbuffer PerTechnique : register(b0)
{
#	if !defined(VR)
	float4 HighDetailRange[1] : packoffset(c0);  // loaded cells center in xy, size in zw
	float2 ParabolaParam : packoffset(c1);       // inverse radius in x, y is 1 for forward hemisphere or -1 for backward hemisphere
#	else
	float4 HighDetailRange[2] : packoffset(c0);  // loaded cells center in xy, size in zw
	float2 ParabolaParam : packoffset(c2);       // inverse radius in x, y is 1 for forward hemisphere or -1 for backward hemisphere
#	endif  // VR
};

cbuffer PerMaterial : register(b1)
{
	float4 TexcoordOffset : packoffset(c0);
};

cbuffer PerGeometry : register(b2)
{
#	if !defined(VR)
	float4 ShadowFadeParam : packoffset(c0);
	row_major float4x4 World[1] : packoffset(c1);
	float4 EyePos[1] : packoffset(c5);
	float4 WaterParams : packoffset(c6);
	float4 TreeParams : packoffset(c7);
#	else
	float4 ShadowFadeParam : packoffset(c0);
	row_major float4x4 World[2] : packoffset(c1);
	float4 EyePos[2] : packoffset(c9);
	float4 WaterParams : packoffset(c11);
	float4 TreeParams : packoffset(c12);
#	endif  // VR
};

float2 SmoothSaturate(float2 value)
{
	return value * value * (3 - 2 * value);
}

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	uint eyeIndex = Stereo::GetEyeIndexVS(
#	if defined(VR)
		input.InstanceID
#	endif
	);

#	if (defined(RENDER_DEPTH) && defined(RENDER_SHADOWMASK_ANY)) || SHADOWFILTER == 2
	vsout.PositionCS.xy = input.PositionMS.xy;
#		if defined(RENDER_SHADOWMASKDPB)
	vsout.PositionCS.z = ShadowFadeParam.z;
#		else
	vsout.PositionCS.z = HighDetailRange[eyeIndex].x;
#		endif
	vsout.PositionCS.w = 1;
#	elif defined(STENCIL_ABOVE_WATER)
	vsout.PositionCS.y = WaterParams.x * 2 + input.PositionMS.y;
	vsout.PositionCS.xzw = input.PositionMS.xzw;
#	else

	precise float4 positionMS = float4(input.PositionMS.xyz, 1.0);
	float4 positionCS = float4(0, 0, 0, 0);

	float3 normalMS = float3(1, 1, 1);
#		if defined(NORMALS)
	normalMS = input.Normal.xyz * 2 - 1;
#		endif

#		if defined(VC) && defined(NORMALS) && defined(TREE_ANIM)
	float2 treeTmp1 = SmoothSaturate(abs(2 * frac(float2(0.1, 0.25) * (TreeParams.w * TreeParams.y * TreeParams.x) + dot(input.PositionMS.xyz, 1.0.xxx) + 0.5) - 1));
	float normalMult = (treeTmp1.x + 0.1 * treeTmp1.y) * (input.Color.w * TreeParams.z);
	positionMS.xyz += normalMS.xyz * normalMult;
#		endif

#		if defined(LOD_LANDSCAPE)
	positionMS = LodLandscape::AdjustLodLandscapeVertexPositionMS(positionMS, World[eyeIndex], HighDetailRange[eyeIndex]);
#		endif

#		if defined(SKINNED)
	precise int4 boneIndices = 765.01.xxxx * input.BoneIndices.xyzw;

	float3x4 worldMatrix = Skinned::GetBoneTransformMatrix(Bones, boneIndices, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, input.BoneWeights);
	precise float4 positionWS = float4(mul(positionMS, transpose(worldMatrix)), 1);

	positionCS = mul(FrameBuffer::CameraViewProj[eyeIndex], positionWS);
#		else
	precise float4x4 modelViewProj = mul(FrameBuffer::CameraViewProj[eyeIndex], World[eyeIndex]);
	positionCS = mul(modelViewProj, positionMS);
#		endif

#		if defined(RENDER_SHADOWMAP) && defined(RENDER_SHADOWMAP_CLAMPED)
	positionCS.z = max(0, positionCS.z);
#		endif

#		if defined(LOD_LANDSCAPE)
	vsout.PositionCS = LodLandscape::AdjustLodLandscapeVertexPositionCS(positionCS);
#		elif defined(RENDER_SHADOWMAP_PB)
	float3 positionCSPerspective = positionCS.xyz / positionCS.w;
	float3 shadowDirection = normalize(normalize(positionCSPerspective) + float3(0, 0, ParabolaParam.y));
	vsout.PositionCS.xy = shadowDirection.xy / shadowDirection.z;
	vsout.PositionCS.z = ParabolaParam.x * length(positionCSPerspective);
	vsout.PositionCS.w = positionCS.w;
#		else
	vsout.PositionCS = positionCS;
#		endif

#		if defined(RENDER_NORMAL)
	float3 normalVS = float3(1, 1, 1);
#			if defined(SKINNED)
	float3x3 boneRSMatrix = Skinned::GetBoneRSMatrix(Bones, boneIndices, input.BoneWeights);
	normalMS = normalize(mul(normalMS, transpose(boneRSMatrix)));
	normalVS = mul(FrameBuffer::CameraView[eyeIndex], float4(normalMS, 0)).xyz;
#			else
	normalVS = mul(mul(FrameBuffer::CameraView[eyeIndex], World[eyeIndex]), float4(normalMS, 0)).xyz;
#			endif
#			if defined(RENDER_NORMAL_CLAMP)
	normalVS = max(min(normalVS, 0.1), -0.1);
#			endif
	vsout.Normal.xyz = normalVS;

#			if defined(VC)
	vsout.Normal.w = input.Color.w;
#			else
	vsout.Normal.w = 1;
#			endif

#		endif

#		if (defined(ALPHA_TEST) && ((!defined(RENDER_DEPTH) && !defined(RENDER_SHADOWMAP)) || defined(RENDER_SHADOWMAP_PB))) || defined(RENDER_NORMAL) || defined(DEBUG_SHADOWSPLIT) || defined(RENDER_BASE_TEXTURE)
	float4 texCoord = float4(0, 0, 1, 1);
	texCoord.xy = input.TexCoord * TexcoordOffset.zw + TexcoordOffset.xy;

#			if defined(RENDER_NORMAL)
	texCoord.z = max(1, 0.0013333333 * positionCS.z + 0.8);

	float falloff = 1;
#				if defined(RENDER_NORMAL_FALLOFF)
#					if defined(SKINNED)
	falloff = dot(normalMS, normalize(EyePos[eyeIndex].xyz - positionWS.xyz));
#					else
	falloff = dot(normalMS, normalize(EyePos[eyeIndex].xyz - positionMS.xyz));
#					endif
#				endif
	texCoord.w = EyePos[eyeIndex].w * falloff;
#			endif

	vsout.TexCoord0 = texCoord;
#		endif

#		if defined(RENDER_SHADOWMAP_PB)
	vsout.TexCoord1.x = ParabolaParam.x * length(positionCSPerspective);
	vsout.TexCoord1.y = positionCS.w;
	precise float parabolaParam = ParabolaParam.y * positionCS.z;
	vsout.TexCoord1.z = parabolaParam * 0.5 + 0.5;
#		elif defined(ALPHA_TEST) && (defined(RENDER_DEPTH) || defined(RENDER_SHADOWMAP))
	float4 texCoord1 = float4(0, 0, 0, 0);
	texCoord1.xy = positionCS.zw;
	texCoord1.zw = input.TexCoord * TexcoordOffset.zw + TexcoordOffset.xy;

	vsout.TexCoord1 = texCoord1;
#		elif defined(ADDITIONAL_ALPHA_MASK)
	vsout.TexCoord1 = positionCS.zw;
#		elif defined(DEBUG_SHADOWSPLIT)
	vsout.Depth = positionCS.z;
#		endif

#		if defined(RENDER_SHADOWMASK_ANY)
	vsout.Alpha.x = 1 - pow(saturate(dot(positionCS.xyz, positionCS.xyz) / ShadowFadeParam.x), 8);

#			if defined(SKINNED)
	vsout.PositionMS.xyz = positionWS.xyz;
#			else
	vsout.PositionMS.xyz = positionMS.xyz;
#			endif
	vsout.PositionMS.w = positionCS.z;
#		endif

#		if (defined(ALPHA_TEST) && defined(VC)) || defined(LOCALMAP_FOGOFWAR)
#			if defined(RENDER_SHADOWMASK_ANY)
	vsout.Alpha.y = input.Color.w;
#			elif !defined(TREE_ANIM)
	vsout.Alpha.x = input.Color.w;
#			endif
#		endif

#	endif

#	if defined(OFFSET_DEPTH)
	vsout.PositionCS.z += 10.0;
#	endif

#	ifdef VR
	vsout.EyeIndex = eyeIndex;
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(vsout.PositionCS, eyeIndex);
	vsout.PositionCS = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#	endif  // VR
	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#ifdef PSHADER

SamplerState SampBaseSampler : register(s0);
SamplerState SampNormalSampler : register(s1);
SamplerState SampDepthSampler : register(s2);
SamplerState SampShadowMapSampler : register(s3);
SamplerComparisonState SampShadowMapSamplerComp : register(s4);
SamplerState SampStencilSampler : register(s5);
SamplerComparisonState SampFocusShadowMapSamplerComp : register(s6);
SamplerState SampGrayscaleSampler : register(s7);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexNormalSampler : register(t1);
Texture2D<float4> TexDepthUtilitySampler : register(t2);
Texture2DArray<float4> TexShadowMapSampler : register(t3);
Texture2DArray<float4> TexShadowMapSamplerComp : register(t4);
Texture2D<uint4> TexStencilSampler : register(t5);
Texture2DArray<float4> TexFocusShadowMapSamplerComp : register(t6);
Texture2D<float4> TexGrayscaleSampler : register(t7);

cbuffer PerTechnique : register(b0)
{
	float4 VPOSOffset : packoffset(c0);
	float4 ShadowSampleParam : packoffset(c1);    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances : packoffset(c2);    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances : packoffset(c3);  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam : packoffset(c4);
}

cbuffer PerMaterial : register(b1)
{
	float RefractionPower : packoffset(c0);
	float4 BaseColor : packoffset(c1);
}

cbuffer PerGeometry : register(b2)
{
	float4 DebugColor : packoffset(c0);
	float4 PropertyColor : packoffset(c1);
	float4 AlphaTestRef : packoffset(c2);
	float4 ShadowLightParam : packoffset(c3);  // Falloff in x, ShadowDistance squared in z
#	if !defined(VR)
	float4x3 FocusShadowMapProj[4] : packoffset(c4);
#		if defined(RENDER_SHADOWMASK)
	float4x3 ShadowMapProj[1][3] : packoffset(c16);  // 16, 19, 22
#		elif defined(RENDER_SHADOWMASKSPOT) || defined(RENDER_SHADOWMASKPB) || defined(RENDER_SHADOWMASKDPB)
	float4x4 ShadowMapProj[1][3] : packoffset(c16);
#		endif
#	else
	float4 VRUnknown : packoffset(c4);  // used to multiply by identity matrix, see e.g., 4202499.ps.bin.hlsl
										/*
										r1.x = dot(cb2[4].xz, icb[r0.w+0].xz);
										r1.x = r0.x * cb12[86].x + -r1.x;
										r0.w = (int)r0.w + 1;
										r0.w = (int)r0.w + -1;
										r0.w = dot(cb2[4].yw, icb[r0.w+0].xz);
										*/
	float4x3 FocusShadowMapProj[4] : packoffset(c5);
#		if defined(RENDER_SHADOWMASK)
	float4x3 ShadowMapProj[2][3] : packoffset(c29);  // VR has a couple of offsets of 3, e.g., {29, 32, 35} and {38, 41, 44}, compare to Flat which does [16, 19, 22]
#		elif defined(RENDER_SHADOWMASKSPOT) || defined(RENDER_SHADOWMASKPB) || defined(RENDER_SHADOWMASKDPB)
	float4x4 ShadowMapProj[2][3] : packoffset(c29);
#		endif
#	endif  // VR
}

#	if !defined(VR)
cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif  // !VR

#	if defined(RENDER_SHADOWMASKDPB)
float GetPoissonDiskFilteredShadowVisibility(float noise, float2x2 rotationMatrix, Texture2DArray<float4> tex, SamplerComparisonState samp, float3 baseUV, float layerIndex, float compareValue, bool asymmetric)
#	else
float GetPoissonDiskFilteredShadowVisibility(float noise, float2x2 rotationMatrix, Texture2DArray<float4> tex, SamplerComparisonState samp, float2 baseUV, float layerIndex, float compareValue, bool asymmetric)
#	endif
{
	const int sampleCount = 16;

#	if defined(RENDER_SHADOWMASK)
	uint onePlusLayerIndex = 1.0 + layerIndex;
	float layerIndexRcp = rcp(onePlusLayerIndex);
#	endif

	float visibility = 0;
	for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
		float2 sampleOffset = mul(Random::PoissonSampleOffsets16[sampleIndex], rotationMatrix);

#	if defined(RENDER_SHADOWMASKDPB)
		float2 sampleUV = baseUV.xy + sampleOffset;
		baseUV.z += noise * 0.5;

		bool lowerHalf = baseUV.z * 0.5 + 0.5 < 0;
		float3 normalizedPositionLS = normalize(float3(sampleUV.xy, baseUV.z));

		float3 positionOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalizedPositionLS + positionOffset);
		float2 shadowMapUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		shadowMapUV.y = lowerHalf ? 1 - 0.5 * shadowMapUV.y : 0.5 * shadowMapUV.y;

		visibility += tex.SampleCmpLevelZero(samp, float3(shadowMapUV, layerIndex), compareValue).x;

#	elif defined(RENDER_SHADOWMASK)
		float2 sampleUV = layerIndexRcp * sampleOffset * ShadowSampleParam.z + baseUV;
		visibility += tex.SampleCmpLevelZero(samp, float3(sampleUV, layerIndex), compareValue).x;
#	else
		float2 sampleUV = sampleOffset * ShadowSampleParam.z + baseUV;
		visibility += tex.SampleCmpLevelZero(samp, float3(sampleUV, layerIndex), compareValue).x;
#	endif
	}
	return visibility * rcp((float)sampleCount);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if !defined(VR)
	uint eyeIndex = 0;
#	else
	uint eyeIndex = input.EyeIndex;
#	endif  // !VR
#	if defined(ADDITIONAL_ALPHA_MASK)
	uint2 alphaMask = input.PositionCS.xy;
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

	if (AlphaTestRef.w - maskValues[alphaMask.x] < 0) {
		discard;
	}
#	endif

	float2 baseTexCoord = 0;
#	if !(defined(RENDER_DEPTH) && defined(RENDER_SHADOWMASK_ANY)) && SHADOWFILTER != 2
#		if (defined(RENDER_DEPTH) || defined(RENDER_SHADOWMAP)) && defined(ALPHA_TEST) && !defined(RENDER_SHADOWMAP_PB)
	baseTexCoord = input.TexCoord1.zw;
#		elif (defined(ALPHA_TEST) && ((!defined(RENDER_DEPTH) && !defined(RENDER_SHADOWMAP)) || defined(RENDER_SHADOWMAP_PB))) || defined(RENDER_NORMAL) || defined(DEBUG_SHADOWSPLIT) || defined(RENDER_BASE_TEXTURE)
	baseTexCoord = input.TexCoord0.xy;
#		endif
#	endif
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, baseTexCoord);

#	if defined(RENDER_SHADOWMAP_PB)
	if (input.TexCoord1.z < 0) {
		discard;
	}
#	endif

	float alpha = 1;
#	if defined(ALPHA_TEST) && !(defined(RENDER_SHADOWMASK_ANY) && defined(RENDER_DEPTH))
	alpha = baseColor.w;
#		if defined(DEPTH_WRITE_DECALS) && !defined(GRAYSCALE_MASK)
	alpha = saturate(1.05 * alpha);
#		endif
#		if defined(OPAQUE_EFFECT)
	alpha *= BaseColor.w * PropertyColor.w;
#		endif
#		if (defined(RENDER_DEPTH) || defined(RENDER_SHADOWMAP) || ((defined(RENDER_NORMAL) || defined(RENDER_NORMAL_CLEAR)) && defined(DEBUG_COLOR))) && defined(VC) && !defined(TREE_ANIM) && !defined(LOD_OBJECT)
	alpha *= input.Alpha;
#		elif defined(RENDER_SHADOWMASK_ANY) && defined(VC)
	alpha *= input.Alpha.y;
#		endif
#		if defined(GRAYSCALE_TO_ALPHA)
	float grayScaleColor = TexGrayscaleSampler.Sample(SampGrayscaleSampler, float2(baseColor.w, alpha)).w;
	if (grayScaleColor - AlphaTestRef.x < 0) {
		discard;
	}
#		endif
#		if defined(GRAYSCALE_MASK)
	if (baseColor.y - AlphaTestRef.x < 0) {
		discard;
	}
#		elif !defined(RENDER_SHADOWMAP)
	if (alpha - AlphaTestRef.x < 0) {
		discard;
	}
#		endif
#	endif

#	if defined(RENDER_SHADOWMASK_ANY)
	float4 shadowColor = 1;

	uint stencilValue = 0;
	float shadowMapDepth = 0;
#		if defined(RENDER_DEPTH)
	float2 depthUv = input.PositionCS.xy * VPOSOffset.xy + VPOSOffset.zw;
	float depth = TexDepthUtilitySampler.Sample(SampDepthSampler, depthUv).x;

	shadowMapDepth = depth;

#			if defined(FOCUS_SHADOW)
	uint3 stencilDimensions;
	TexStencilSampler.GetDimensions(0, stencilDimensions.x, stencilDimensions.y, stencilDimensions.z);
	stencilValue = TexStencilSampler.Load(float3(stencilDimensions.xy * depthUv, 0)).x;
#			endif
	depthUv = Stereo::ConvertFromStereoUV(depthUv * FrameBuffer::DynamicResolutionParams2.xy, eyeIndex);
	float4 positionCS = float4(2 * float2(depthUv.x, -depthUv.y + 1) - 1, depth, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
	positionMS.xyz = positionMS.xyz / positionMS.w;

	float fadeFactor = 1 - pow(saturate(dot(positionMS.xyz, positionMS.xyz) / ShadowLightParam.z), 8);
#		else
	float4 positionMS = input.PositionMS.xyzw;

	shadowMapDepth = positionMS.w;

	float fadeFactor = input.Alpha.x;
#		endif

	float noise = Random::InterleavedGradientNoise(input.PositionCS.xy, SharedData::FrameCount);

	float2 rotation;
	sincos(Math::TAU * noise, rotation.y, rotation.x);
	float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

	noise = noise * 2.0 - 1.0;

#		if defined(RENDER_SHADOWMASK)
	if (EndSplitDistances.z >= shadowMapDepth) {
		float4x3 lightProjectionMatrix = ShadowMapProj[eyeIndex][0];
		float shadowMapThreshold = AlphaTestRef.y;
		float cascadeIndex = 0;
		if (2.5 < EndSplitDistances.w && EndSplitDistances.y < shadowMapDepth) {
			lightProjectionMatrix = ShadowMapProj[eyeIndex][2];
			shadowMapThreshold = AlphaTestRef.z;
			cascadeIndex = 2;
		} else if (EndSplitDistances.x < shadowMapDepth) {
			lightProjectionMatrix = ShadowMapProj[eyeIndex][1];
			shadowMapThreshold = AlphaTestRef.z;
			cascadeIndex = 1;
		}

		float shadowVisibility = 0;

		float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionMS.xyz, 1)).xyz;

#			if SHADOWFILTER == 0
		float shadowMapValue = TexShadowMapSampler.Sample(SampShadowMapSampler, float3(positionLS.xy, cascadeIndex)).x;
		if (shadowMapValue >= positionLS.z - shadowMapThreshold) {
			shadowVisibility = 1;
		}
#			elif SHADOWFILTER == 1
		shadowVisibility = TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(positionLS.xy, cascadeIndex), positionLS.z - shadowMapThreshold).x;
#			elif SHADOWFILTER == 3
		shadowVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexShadowMapSamplerComp, SampShadowMapSamplerComp, positionLS.xy, cascadeIndex, positionLS.z - shadowMapThreshold, false);
#			endif

		if (cascadeIndex < 1 && StartSplitDistances.y < shadowMapDepth) {
			float cascade1ShadowVisibility = 0;

			float3 cascade1PositionLS = mul(transpose(ShadowMapProj[eyeIndex][1]), float4(positionMS.xyz, 1)).xyz;

#			if SHADOWFILTER == 0
			float cascade1ShadowMapValue = TexShadowMapSampler.Sample(SampShadowMapSampler, float3(cascade1PositionLS.xy, 1)).x;
			if (cascade1ShadowMapValue >= cascade1PositionLS.z - AlphaTestRef.z) {
				cascade1ShadowVisibility = 1;
			}
#			elif SHADOWFILTER == 1
			cascade1ShadowVisibility = TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(cascade1PositionLS.xy, 1), cascade1PositionLS.z - AlphaTestRef.z).x;
#			elif SHADOWFILTER == 3
			cascade1ShadowVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexShadowMapSamplerComp, SampShadowMapSamplerComp, cascade1PositionLS.xy, 1, cascade1PositionLS.z - AlphaTestRef.z, false);
#			endif

			float cascade1BlendFactor = smoothstep(0, 1, (shadowMapDepth - StartSplitDistances.y) / (EndSplitDistances.x - StartSplitDistances.y));
			shadowVisibility = lerp(shadowVisibility, cascade1ShadowVisibility, cascade1BlendFactor);

			shadowMapThreshold = AlphaTestRef.z;
		}

		if (stencilValue != 0) {
			uint focusShadowIndex = stencilValue - 1;
			float3 focusShadowMapPosition = mul(transpose(FocusShadowMapProj[focusShadowIndex]), float4(positionMS.xyz, 1));
			float3 focusShadowMapUv = float3(focusShadowMapPosition.xy, StartSplitDistances.w + focusShadowIndex);
			float focusShadowMapCompareValue = focusShadowMapPosition.z - 3 * shadowMapThreshold;
#			if SHADOWFILTER == 3
			float focusShadowVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexFocusShadowMapSamplerComp, SampFocusShadowMapSamplerComp, focusShadowMapUv.xy, focusShadowMapUv.z, focusShadowMapCompareValue, false).x;
#			else
			float focusShadowVisibility = TexFocusShadowMapSamplerComp.SampleCmpLevelZero(SampFocusShadowMapSamplerComp, focusShadowMapUv, focusShadowMapCompareValue).x;
#			endif
			float focusShadowFade = FocusShadowFadeParam[focusShadowIndex];
			shadowVisibility = min(shadowVisibility, lerp(1, focusShadowVisibility, focusShadowFade));
		}

		shadowColor.xyzw = fadeFactor * (shadowVisibility - 1) + 1;
	}
#		elif defined(RENDER_SHADOWMASKSPOT)
	float4 positionLS = mul(transpose(ShadowMapProj[eyeIndex][0]), float4(positionMS.xyz, 1));
	positionLS.xyz /= positionLS.w;
	float2 shadowMapUv = positionLS.xy * 0.5 + 0.5;
	float shadowBaseVisibility = 0;
#			if SHADOWFILTER == 0
	float shadowMapValue = TexShadowMapSampler.Sample(SampShadowMapSampler, float3(shadowMapUv, EndSplitDistances.x)).x;
	if (shadowMapValue >= positionLS.z - AlphaTestRef.y) {
		shadowBaseVisibility = 1;
	}
#			elif SHADOWFILTER == 1
	shadowBaseVisibility = TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(shadowMapUv, EndSplitDistances.x), positionLS.z - AlphaTestRef.y).x;
#			elif SHADOWFILTER == 3
	shadowBaseVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexShadowMapSamplerComp, SampShadowMapSamplerComp, shadowMapUv.xy, EndSplitDistances.x, positionLS.z - AlphaTestRef.y, false);
#			endif
	float shadowVisibilityFactor = pow(2 * length(0.5 * positionLS.xy), ShadowLightParam.x);
	float shadowVisibility = shadowBaseVisibility - shadowVisibilityFactor * shadowBaseVisibility;

	if (stencilValue != 0) {
		uint focusShadowIndex = stencilValue - 1;
		float3 focusShadowMapPosition = mul(transpose(FocusShadowMapProj[focusShadowIndex]), float4(positionMS.xyz, 1));
		float3 focusShadowMapUv = float3(focusShadowMapPosition.xy, StartSplitDistances.w + focusShadowIndex);
		float focusShadowMapCompareValue = focusShadowMapPosition.z - 3 * AlphaTestRef.y;
		float focusShadowVisibility = 0;
#			if SHADOWFILTER == 0
		float shadowMapValue = TexShadowMapSampler.Sample(SampShadowMapSampler, focusShadowMapUv).x;
		if (shadowMapValue >= focusShadowMapCompareValue) {
			focusShadowVisibility = 1;
		}
#			elif SHADOWFILTER == 1
		focusShadowVisibility = TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, focusShadowMapUv, focusShadowMapCompareValue).x;
#			elif SHADOWFILTER == 3
		focusShadowVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexShadowMapSamplerComp, SampShadowMapSamplerComp, focusShadowMapUv.xy, focusShadowMapUv.z, focusShadowMapCompareValue, false);
#			endif
		shadowVisibility = min(shadowVisibility, lerp(1, focusShadowVisibility, FocusShadowFadeParam[focusShadowIndex]));
	}

	shadowColor.xyzw = fadeFactor * shadowVisibility;
#		elif defined(RENDER_SHADOWMASKPB)
	float4 unadjustedPositionLS = mul(transpose(ShadowMapProj[eyeIndex][0]), float4(positionMS.xyz, 1));

	float shadowVisibility = 0;

	if (unadjustedPositionLS.z * 0.5 + 0.5 >= 0) {
		float3 positionLS = unadjustedPositionLS.xyz / unadjustedPositionLS.w;
		float3 lightDirection = normalize(normalize(positionLS) + float3(0, 0, 1));
		float2 shadowMapUv = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		float shadowMapCompareValue = saturate(length(positionLS) / ShadowLightParam.x) - AlphaTestRef.y;
#			if SHADOWFILTER == 0
		float shadowMapValue = TexShadowMapSampler.Sample(SampShadowMapSampler, float3(shadowMapUv, EndSplitDistances.x)).x;
		if (shadowMapValue >= shadowMapCompareValue) {
			shadowVisibility = 1;
		}
#			elif SHADOWFILTER == 1
		shadowVisibility = TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(shadowMapUv, EndSplitDistances.x), shadowMapCompareValue).x;
#			elif SHADOWFILTER == 3
		shadowVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexShadowMapSamplerComp, SampShadowMapSamplerComp, shadowMapUv.xy, EndSplitDistances.x, shadowMapCompareValue, false);
#			endif
	} else {
		shadowVisibility = 1;
	}

	shadowColor.xyzw = fadeFactor * shadowVisibility;
#		elif defined(RENDER_SHADOWMASKDPB)
	float3 positionLS = mul(transpose(ShadowMapProj[eyeIndex][0]), float4(positionMS.xyz, 1)).xyz;

	bool lowerHalf = positionLS.z * 0.5 + 0.5 < 0;
	float3 normalizedPositionLS = normalize(positionLS);

	float shadowMapCompareValue = saturate(length(positionLS) / ShadowLightParam.x) - AlphaTestRef.y;

	float3 positionOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
	float3 lightDirection = normalize(normalizedPositionLS + positionOffset);
	float2 shadowMapUv = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
	shadowMapUv.y = lowerHalf ? 1 - 0.5 * shadowMapUv.y : 0.5 * shadowMapUv.y;

	float shadowVisibility = 0;
#			if SHADOWFILTER == 0
	float shadowMapValue = TexShadowMapSampler.Sample(SampShadowMapSampler, float3(shadowMapUv, EndSplitDistances.x)).x;
	if (shadowMapValue >= shadowMapCompareValue) {
		shadowVisibility = 1;
	}
#			elif SHADOWFILTER == 1
	shadowVisibility = TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(shadowMapUv, EndSplitDistances.x), shadowMapCompareValue).x;
#			elif SHADOWFILTER == 3
	shadowVisibility = GetPoissonDiskFilteredShadowVisibility(noise, rotationMatrix, TexShadowMapSamplerComp, SampShadowMapSamplerComp, positionLS.xyz, EndSplitDistances.x, shadowMapCompareValue, true);
#			endif

	shadowColor.xyzw = fadeFactor * shadowVisibility;
#		endif
#	endif

#	if defined(TEXTURE)
	float testAlpha = 1;
#		if defined(RENDER_SHADOWMASK_ANY)
	testAlpha = shadowColor.w;
#		elif defined(DEBUG_SHADOWSPLIT) || defined(RENDER_BASE_TEXTURE)
	testAlpha = baseColor.w;
#		elif defined(LOCALMAP_FOGOFWAR)
	testAlpha = input.Alpha;
#		elif (defined(RENDER_DEPTH) || defined(RENDER_SHADOWMAP)) && defined(ALPHA_TEST)
	testAlpha = alpha;
#		elif defined(STENCIL_ABOVE_WATER)
	testAlpha = 0.5;
#		elif defined(RENDER_NORMAL_CLEAR) && !defined(DEBUG_COLOR)
	testAlpha = 0;
#		endif
	if (-AlphaTestRefRS + testAlpha < 0) {
		discard;
	}
#	endif

#	if defined(RENDER_SHADOWMASK_ANY)
	psout.Color.xyzw = shadowColor;
#	elif defined(DEBUG_SHADOWSPLIT)

	float3 splitFactor = 0;

	if (input.Depth < EndSplitDistances.x) {
		splitFactor.y += 1;
	}
	if (input.Depth < EndSplitDistances.w && input.Depth > EndSplitDistances.z) {
		splitFactor.y += 1;
		splitFactor.z += 1;
	}
	if (input.Depth < EndSplitDistances.y && input.Depth > EndSplitDistances.x) {
		splitFactor.z += 1;
	}
	if (input.Depth < EndSplitDistances.z && input.Depth > EndSplitDistances.y) {
		splitFactor.x += 1;
		splitFactor.y += 1;
	}

#		if defined(DEBUG_COLOR)
	psout.Color.xyz = (-splitFactor.xyz + DebugColor.xyz) * 0.9 + splitFactor.xyz;
#		else
	psout.Color.xyz = (-splitFactor.xyz + baseColor.xyz) * 0.9 + splitFactor.xyz;
#		endif

	psout.Color.w = baseColor.w;
#	elif defined(DEBUG_COLOR)
	psout.Color = float4(DebugColor.xyz, 1);
#	elif defined(RENDER_BASE_TEXTURE)
	psout.Color.xyzw = baseColor;
#	elif defined(LOCALMAP_FOGOFWAR)
	psout.Color = float4(0, 0, 0, input.Alpha);
#	elif defined(GRAYSCALE_MASK)
	psout.Color = float4(input.TexCoord1.x / input.TexCoord1.y, baseColor.yz, alpha);
#	elif (defined(RENDER_SHADOWMAP) && defined(ALPHA_TEST)) || defined(RENDER_SHADOWMAP_PB)
	psout.Color = float4(input.TexCoord1.xxx / input.TexCoord1.yyy, alpha);
#	elif defined(RENDER_DEPTH) && (defined(ALPHA_TEST) || defined(ADDITIONAL_ALPHA_MASK))
	psout.Color = float4(input.TexCoord1.x / input.TexCoord1.y, 1, 1, alpha);
#	elif defined(STENCIL_ABOVE_WATER)
	psout.Color = float4(1, 0, 0, 0.5);
#	elif defined(RENDER_NORMAL_CLEAR)
	psout.Color = float4(0.5, 0.5, 0, 0);
#	elif defined(RENDER_NORMAL)
	float2 normal = 2 * (-0.5 + TexNormalSampler.Sample(SampNormalSampler, input.TexCoord0.xy).xy);
#		if defined(RENDER_NORMAL_CLAMP)
	normal = clamp(normal, -0.1, 0.1);
#		endif
	psout.Color.xy = ((normal * 0.9 + input.Normal.xy) / input.TexCoord0.z) * 0.5 + 0.5;
	psout.Color.z = input.Normal.w * (RefractionPower * input.TexCoord0.w);
	psout.Color.w = 1;
#	else
	psout.Color = float4(1, 1, 1, 1);
#	endif

	return psout;
}

#endif
