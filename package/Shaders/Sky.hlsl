#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/SharedData.hlsli"

struct VS_INPUT
{
	float4 Position: POSITION0;

#if defined(TEX) || defined(HORIZFADE)
	float2 TexCoord: TEXCOORD0;
#endif

	float4 Color: COLOR0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;

#if defined(DITHER) && defined(TEX)
	float4 TexCoord0: TEXCOORD0;
#elif defined(DITHER)
	float2 TexCoord0: TEXCOORD3;
#elif defined(TEX) || defined(HORIZFADE)
	float2 TexCoord0: TEXCOORD0;
#endif

#if defined(TEXLERP)
	float2 TexCoord1: TEXCOORD1;
#endif

#if defined(HORIZFADE)
	float TexCoord2: TEXCOORD2;
#endif

#if defined(TEX) || defined(DITHER) || defined(HORIZFADE)
	float4 Color: COLOR0;
#endif

#if !defined(OCCLUSION) && !defined(MOONMASK) && !defined(HORIZFADE)
	float4 SkyBlendColor0: TEXCOORD5;
	float4 SkyBlendColor2: TEXCOORD6;
#endif

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float3 FogPosition: TEXCOORD4;
};

#ifdef VSHADER
cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 World : packoffset(c4);
	row_major float4x4 PreviousWorld : packoffset(c8);
	float3 EyePosition : packoffset(c12);
	float VParams : packoffset(c12.w);
	float4 BlendColor[3] : packoffset(c13);
	float2 TexCoordOff : packoffset(c16);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);

#	if defined(OCCLUSION)

	// Intentionally left blank

#	elif defined(MOONMASK)

	vsout.TexCoord0 = input.TexCoord;
	vsout.Color = float4(VParams.xxx, 1.0);

#	elif defined(HORIZFADE)

	float worldHeight = mul(World, inputPosition).z;
	float eyeHeightDelta = -EyePosition.z + worldHeight;

	vsout.TexCoord0.xy = input.TexCoord;
	vsout.TexCoord2.x = saturate((1.0 / 17.0) * eyeHeightDelta);
	vsout.Color.xyz = BlendColor[0].xyz * VParams;
	vsout.Color.w = BlendColor[0].w;

#	else  // MOONMASK HORIZFADE

#		if defined(DITHER)

#			if defined(TEX)
	vsout.TexCoord0.xyzw = input.TexCoord.xyxy * float4(1.0, 1.0, 501.0, 501.0);
#			else
	float3 inputDirection = normalize(input.Position.xyz);
	inputDirection.y += inputDirection.z;

	vsout.TexCoord0.x = 501 * acos(inputDirection.x);
	vsout.TexCoord0.y = 501 * asin(inputDirection.y);
#			endif  // TEX

#		elif defined(CLOUDS)
	vsout.TexCoord0.xy = TexCoordOff + input.TexCoord;
#		else
	vsout.TexCoord0.xy = input.TexCoord;
#		endif  // DITHER CLOUDS

#		ifdef TEXLERP
	vsout.TexCoord1.xy = TexCoordOff + input.TexCoord;
#		endif  // TEXLERP

	float3 skyColor = BlendColor[0].xyz * input.Color.xxx + BlendColor[1].xyz * input.Color.yyy +
	                  BlendColor[2].xyz * input.Color.zzz;

	vsout.Color.xyz = VParams * skyColor;
	vsout.Color.w = BlendColor[0].w * input.Color.w;
	vsout.SkyBlendColor0 = float4(BlendColor[0].xyz * VParams, 0);
	vsout.SkyBlendColor2 = float4(BlendColor[2].xyz * VParams, 0);
#	endif      // OCCLUSION MOONMASK HORIZFADE

	vsout.Position = mul(WorldViewProj, inputPosition).xyww;
	vsout.WorldPosition = mul(World, inputPosition);
	vsout.FogPosition = vsout.WorldPosition.xyz - EyePosition.xyz;
	vsout.PreviousWorldPosition = mul(PreviousWorld, inputPosition);

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 Normal: SV_Target2;
#if defined(CLOUD_SHADOWS) && defined(CLOUDS) && !defined(DEFERRED)
	float4 CloudShadows: SV_Target3;
#endif
};

#ifdef PSHADER
SamplerState SampBaseSampler : register(s0);
SamplerState SampBlendSampler : register(s1);
SamplerState SampNoiseGradSampler : register(s2);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexBlendSampler : register(t1);
Texture2D<float4> TexNoiseGradSampler : register(t2);

cbuffer PerGeometry : register(b2)
{
	float2 PParams : packoffset(c0);
};

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

#	include "Common/MotionBlur.hlsli"
#	include "Common/SharedData.hlsli"

#	if defined(EXP_HEIGHT_FOG)
#		define SampColorSampler SampBaseSampler
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

#	ifdef HDR_OUTPUT
#		include "HDRDisplay/HDRSun.hlsli"
#	endif

Texture2D<float> TexDepthSampler : register(t17);

#	if defined(EFFECTS11)
float ComputeProceduralSun(float2 uv)
{
	float2 p = uv * 2.0 - 1.0;
	float dist = dot(p, p) - SharedData::enbSettings.ProceduralSunDiskRadiusSq;

	float c = saturate(dist * SharedData::enbSettings.ProceduralSunCoronaScale);
	float corona = (1.0 - c) * rcp(SharedData::enbSettings.ProceduralSunCoronaFalloff * c + 1.0) * SharedData::enbSettings.ProceduralSunGlowIntensity;

	float disk = saturate(-dist * SharedData::enbSettings.ProceduralSunDiskEdgeScale);

	return corona + disk;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	// Color::Sky is float3->float3 (per-channel sky gamma). PParams.yyy broadcasts the packed
	// scalar in PParams.y to RGB; float3 matches output .xyz where skyScale is added.
	float3 skyScale = Color::Sky(PParams.yyy);

#	ifndef OCCLUSION
#		ifndef TEXLERP
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	baseColor.xyz = Color::Sky(baseColor.xyz);
#			ifdef TEXFADE
	baseColor.w *= PParams.x;
#			endif
#		else
	float4 blendColor = TexBlendSampler.Sample(SampBlendSampler, input.TexCoord1.xy);
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	blendColor.xyz = Color::Sky(blendColor.xyz);
	baseColor.xyz = Color::Sky(baseColor.xyz);
	baseColor = PParams.xxxx * (-baseColor + blendColor) + baseColor;
#		endif

#		if defined(HDR_OUTPUT)
	float hdrSunGain = HDRSun::GetHdrSunGain(input.TexCoord0.xy, baseColor);
	baseColor.xyz *= hdrSunGain;
#		endif

#		if defined(TEX) && defined(EFFECTS11)
	if (SharedData::enbSettings.EnableProceduralSun && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun)) {
		baseColor.xyz = ComputeProceduralSun(input.TexCoord0.xy);
		baseColor.w = input.Color.w;
		skyScale = 0.0;
	}
#		endif

#		if defined(DITHER)
	float2 noiseGradUv = float2(0.125, 0.125) * input.Position.xy;
	float noiseGrad = TexNoiseGradSampler.Sample(SampNoiseGradSampler, noiseGradUv).x * 0.03125 - 0.0078125;
	noiseGrad *= 10.0;

#			ifdef TEX
	psout.Color.xyz = Color::Sky(input.Color.xyz) * baseColor.xyz + skyScale;
	psout.Color.xyz *= 1.0 + noiseGrad;
	psout.Color.w = baseColor.w * input.Color.w;
#			else
	float3 skyGradientColor = input.Color.xyz;

#if defined(EFFECTS11)
	float3 viewDirection = normalize(input.WorldPosition.xyz);
	if (SharedData::enbSettings.UseProceduralGradientWeights) {
		float gradientPosition = pow(1.0 - saturate(viewDirection.z), SharedData::enbSettings.ProceduralGradientWeightCurve);
		skyGradientColor = lerp(input.SkyBlendColor2.xyz, input.SkyBlendColor0.xyz, gradientPosition);
	}
#endif
	psout.Color.xyz = Color::Sky(skyGradientColor) + skyScale;

	psout.Color.xyz *= 1.0 + noiseGrad;
	psout.Color.w = input.Color.w;
#			endif  // TEX

#		elif defined(MOONMASK)
	psout.Color.xyzw = baseColor;

	if (baseColor.w - AlphaTestRefRS.x < 0) {
		discard;
	}

#		elif defined(HORIZFADE)
	psout.Color.xyz = float3(1.5, 1.5, 1.5) * (Color::Sky(input.Color.xyz) * baseColor.xyz + skyScale);
	psout.Color.w = input.TexCoord2.x * (baseColor.w * input.Color.w);
#		else

#		if defined(CLOUDS) && defined(EFFECTS11)
	if (SharedData::enbSettings.Enable)
		baseColor.xyz = pow(abs(baseColor.xyz), SharedData::enbSettings.CloudsCurve);
#		endif

	psout.Color.w = input.Color.w * baseColor.w;
	psout.Color.xyz = Color::Sky(input.Color.xyz) * baseColor.xyz + skyScale;

#			if defined(CLOUDS) && defined(EFFECTS11)
	if (SharedData::enbSettings.Enable) {
		float3 cloudColor = psout.Color.xyz;
		float3 viewDirection = normalize(input.WorldPosition.xyz);

		cloudColor.xyz = lerp(abs(cloudColor.xyz), dot(cloudColor.xyz, 1.0 / 3.0), SharedData::enbSettings.CloudsDesaturation);

		float cloudLuminance = dot(cloudColor.xyz, 1.0 / 3.0);

		float sunLighting = saturate(dot(viewDirection, SharedData::SunDirection.xyz) * 0.5 + 0.5);
		float masserLighting = saturate(dot(viewDirection, SharedData::MasserDirection.xyz) * 0.5 + 0.5);
		float secundaLighting = saturate(dot(viewDirection, SharedData::SecundaDirection.xyz) * 0.5 + 0.5);

		if (SharedData::enbSettings.CloudsEdgeIntensity > 0.0) {
			float cloudsEdgeAlpha = saturate(1.0 - baseColor.w);
			
			float3 sunPhase = pow(sunLighting, 32.0) * SharedData::SunColor.xyz * cloudsEdgeAlpha;
			float3 masserPhase = pow(masserLighting, 32.0) * SharedData::MasserColor.xyz * SharedData::enbSettings.CloudsEdgeMoonMultiplier * cloudsEdgeAlpha;
			float3 secundaPhase = pow(secundaLighting, 32.0) * SharedData::SecundaColor.xyz * SharedData::enbSettings.CloudsEdgeMoonMultiplier * cloudsEdgeAlpha;

			float3 cloudsScatter = (sunPhase + masserPhase + secundaPhase) * SharedData::enbSettings.CloudsEdgeIntensity;

			cloudColor += cloudLuminance * cloudsScatter;
		}

		psout.Color.xyz = cloudColor;
		psout.Color.w = saturate(psout.Color.w);
	}
#			endif
#		endif

#	else
	psout.Color = float4(0, 0, 0, 1.0);
#	endif  // OCCLUSION

#	if defined(EXP_HEIGHT_FOG)
	const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) != 0;
	if (inReflection && SharedData::exponentialHeightFogSettings.enabled) {
		float3 skyFogPosition = normalize(input.FogPosition.xyz) * SharedData::CameraData.x;
		float4 exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFogNoVolumetric(skyFogPosition, FrameBuffer::CameraPosAdjust.xyz, psout.Color.xyz, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		psout.Color.xyz = lerp(psout.Color.xyz, exponentialHeightFog.xyz, exponentialHeightFog.w);
	}
#	endif

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	psout.MotionVectors = float4(screenMotionVector, 0, psout.Color.w);
	psout.Normal = float4(0.5, 0.5, 0, psout.Color.w);

#	if defined(CLOUD_SHADOWS) && defined(CLOUDS) && !defined(DEFERRED)
	psout.CloudShadows = psout.Color.w;

	// Keep sun behind scene depth to prevent halo leaks through geometry.
	float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
	if (depth < input.Position.z)
		psout.Color.w = 0;

#	else
	// Even without cloud shadows enabled, sun disc should be occluded by scene depth (clouds, terrain, etc.)
	if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun)) {
		float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
		if (depth < input.Position.z)
			psout.Color.w = 0;
	}
#	endif

	return psout;
}
#endif
