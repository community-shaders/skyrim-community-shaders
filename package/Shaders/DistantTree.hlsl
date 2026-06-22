#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

struct VS_INPUT
{
	float3 Position: POSITION0;
	float2 TexCoord0: TEXCOORD0;
	float4 InstanceData1: TEXCOORD4;
	float4 InstanceData2: TEXCOORD5;
	float4 InstanceData3: TEXCOORD6;
	float4 InstanceData4: TEXCOORD7;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
	float3 TexCoord: TEXCOORD0;

#if defined(RENDER_DEPTH)
	float4 Depth: TEXCOORD3;
#else
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
#endif  // RENDER_DEPTH
	float4 ViewPosition: POSITION3;

};

#ifdef VSHADER
cbuffer PerTechnique : register(b0)
{
	float4 FogParam : packoffset(c0);
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 World : packoffset(c4);
	row_major float4x4 PreviousWorld : packoffset(c8);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout = (VS_OUTPUT)0;

	float3 scaledModelPosition = input.InstanceData1.www * input.Position.xyz;
	float3 adjustedModelPosition = 0.0.xxx;
	adjustedModelPosition.x = dot(float2(1, -1) * input.InstanceData2.xy, scaledModelPosition.xy);
	adjustedModelPosition.y = dot(input.InstanceData2.yx, scaledModelPosition.xy);
	adjustedModelPosition.z = scaledModelPosition.z;
	float4 finalModelPosition = float4(input.InstanceData1.xyz + adjustedModelPosition.xyz, 1.0);
	float4 viewPosition = mul(WorldViewProj, finalModelPosition);

#	ifdef RENDER_DEPTH
	vsout.Depth.xy = viewPosition.zw;
	vsout.Depth.zw = input.InstanceData2.zw;
#	else
	vsout.WorldPosition = mul(World, finalModelPosition);
	vsout.PreviousWorldPosition = mul(PreviousWorld, finalModelPosition);
	vsout.ViewPosition = viewPosition;
#	endif  // RENDER_DEPTH

	vsout.Position = viewPosition;
	vsout.TexCoord = float3(input.TexCoord0.xy, FogParam.z);

	return vsout;
}
#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;

#if !defined(RENDER_DEPTH)
#	if defined(DEFERRED)
	float2 MotionVector: SV_Target1;
	float4 Normal: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Masks: SV_Target6;
#	endif  // DEFERRED
#endif      // !RENDER_DEPTH
};

#ifdef PSHADER
SamplerState SampDiffuse : register(s0);

Texture2D<float4> TexDiffuse : register(t0);

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

cbuffer PerFrame : register(b12)
{
	float4 UnknownPerFrame1[12] : packoffset(c0);
	row_major float4x4 ScreenProj : packoffset(c12);
	row_major float4x4 PreviousScreenProj : packoffset(c16);
}

cbuffer PerTechnique : register(b0)
{
	float4 DiffuseColor : packoffset(c0);
	float4 AmbientColor : packoffset(c1);
};

const static float DepthOffsets[16] = {
	0.003921568,
	0.533333361,
	0.133333340,
	0.666666687,
	0.800000000,
	0.266666681,
	0.933333337,
	0.400000000,
	0.200000000,
	0.733333349,
	0.066666670,
	0.600000000,
	0.996078432,
	0.466666669,
	0.866666675,
	0.333333343
};

#	if defined(SCREEN_SPACE_SHADOWS)
#		include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#	endif

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(EXP_HEIGHT_FOG)
#		define SampColorSampler SampDiffuse
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

#	define LinearSampler SampDiffuse

#	include "Common/ShadowSampling.hlsli"

#	if defined(EXP_HEIGHT_FOG)
void ApplyReflectionExponentialHeightFog(inout float3 color, float3 positionWS, float4 screenPosition)
{
	float3 fogColor = Color::Fog(AmbientColor.xyz);
	float4 exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFogNoVolumetric(positionWS, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(screenPosition.xy * FrameBuffer::DynamicResolutionParams2.xy, screenPosition.z, 1));
	color = lerp(color, exponentialHeightFog.xyz, exponentialHeightFog.w);
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(EXP_HEIGHT_FOG)
	const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) != 0;
#	endif

#	if defined(RENDER_DEPTH)
	uint2 temp = uint2(input.Position.xy);
	uint index = ((temp.x << 2) & 12) | (temp.y & 3);

	float depthOffset = 0.5 - DepthOffsets[index];
	float depthModifier = (input.Depth.w * depthOffset) + input.Depth.z - 0.5;

	if (depthModifier < 0) {
		discard;
	}

	float alpha = TexDiffuse.SampleBias(SampDiffuse, input.TexCoord.xy, SharedData::MipBias).w;

	if ((alpha - AlphaTestRefRS) < 0) {
		discard;
	}

	psout.Diffuse.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.Diffuse.w = 0;
#	else
	float4 baseColor = TexDiffuse.SampleBias(SampDiffuse, input.TexCoord.xy, SharedData::MipBias);
	baseColor.xyz = Color::Diffuse(baseColor.xyz);

	if ((baseColor.w - AlphaTestRefRS) < 0) {
		discard;
	}

#		if defined(DEFERRED)
	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.Position.xy, SharedData::FrameCount);

	float dirShadow = 1;

#			if defined(SCREEN_SPACE_SHADOWS)
	dirShadow = lerp(1.0, ScreenSpaceShadows::GetScreenSpaceShadow(input.Position.xyz, screenUV, screenNoise), 0.8);
#			endif

	if (dirShadow != 0.0)
		dirShadow *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 diffuseColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * dirShadow * 0.5 * llDirLightMult * Color::VanillaNormalization();

#			if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		diffuseColor *= ExponentialHeightFog::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#			endif

	float3 ddx = ddx_coarse(input.WorldPosition.xyz);
	float3 ddy = ddy_coarse(input.WorldPosition.xyz);
	float3 normal = -normalize(cross(ddx, ddy));

	float3 directionalAmbientColor = max(0, Color::Ambient(SharedData::GetAmbient(normal)));
#			if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
	}
#			endif
	diffuseColor += directionalAmbientColor;

	psout.Diffuse.xyz = diffuseColor * baseColor.xyz;
	psout.Diffuse.w = 1;

#			if defined(EXP_HEIGHT_FOG)
	if (inReflection && SharedData::exponentialHeightFogSettings.enabled) {
		ApplyReflectionExponentialHeightFog(psout.Diffuse.xyz, input.WorldPosition.xyz, input.Position);
	}
#			endif

	psout.MotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	psout.Normal.xy = GBuffer::EncodeNormal(FrameBuffer::WorldToView(normal, false));
	psout.Normal.zw = 0;

	psout.Albedo = float4(baseColor.xyz, 1);
	psout.Masks = float4(0, 0, 1, 0);
#		else
	float dirShadow = ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 diffuseColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * dirShadow * 0.5 * llDirLightMult * Color::VanillaNormalization();

#			if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		diffuseColor *= ExponentialHeightFog::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#			endif

	float3 ddx = ddx_coarse(input.WorldPosition.xyz);
	float3 ddy = ddy_coarse(input.WorldPosition.xyz);
	float3 normal = normalize(cross(ddx, ddy));

	float3 directionalAmbientColor = Color::Ambient(SharedData::GetAmbient(normal));
#			if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
	}
#			endif
	diffuseColor += directionalAmbientColor;

	float3 color = diffuseColor * baseColor.xyz;
#			if defined(EXP_HEIGHT_FOG)
	if (inReflection && SharedData::exponentialHeightFogSettings.enabled) {
		ApplyReflectionExponentialHeightFog(color, input.WorldPosition.xyz, input.Position);
	}
#			endif
	psout.Diffuse = float4(color, 1.0);
#		endif  // DEFERRED
#	endif      // RENDER_DEPTH

	return psout;
}
#endif  // PSHADER
