#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

struct VS_INPUT
{
	float4 Position: POSITION0;

#if defined(TEX) || defined(HORIZFADE)
	float2 TexCoord: TEXCOORD0;
#endif

	float4 Color: COLOR0;
#if defined(VR)
	uint InstanceID: SV_INSTANCEID;
#endif  // VR
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

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
#if defined(VR)
	float ClipDistance: SV_ClipDistance0;  // o11
	float CullDistance: SV_CullDistance0;  // p11
	uint EyeIndex: EYEIDX0;
#endif  // VR
};

#ifdef VSHADER
cbuffer PerGeometry : register(b2)
{
#	if !defined(VR)
	row_major float4x4 WorldViewProj[1] : packoffset(c0);
	row_major float4x4 World[1] : packoffset(c4);
	row_major float4x4 PreviousWorld[1] : packoffset(c8);
	float3 EyePosition[1] : packoffset(c12);
	float VParams : packoffset(c12.w);
	float4 BlendColor[3] : packoffset(c13);
	float2 TexCoordOff : packoffset(c16);
#	else
	row_major float4x4 WorldViewProj[2] : packoffset(c0);
	row_major float4x4 World[2] : packoffset(c8);
	row_major float4x4 PreviousWorld[2] : packoffset(c16);
	float3 EyePosition[2] : packoffset(c24);
	float VParams : packoffset(c25.w);
	float4 BlendColor[3] : packoffset(c26);
	float2 TexCoordOff : packoffset(c29);
#	endif  // !VR
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	uint eyeIndex = Stereo::GetEyeIndexVS(
#	if defined(VR)
		input.InstanceID
#	endif
	);

	float4 inputPosition = float4(input.Position.xyz, 1.0);

#	if defined(OCCLUSION)

	// Intentionally left blank

#	elif defined(MOONMASK)

	vsout.TexCoord0 = input.TexCoord;
	vsout.Color = float4(VParams.xxx, 1.0);

#	elif defined(HORIZFADE)

	float worldHeight = mul(World[eyeIndex], inputPosition).z;
	float eyeHeightDelta = -EyePosition[eyeIndex].z + worldHeight;

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

#	endif  // OCCLUSION MOONMASK HORIZFADE

	vsout.Position = mul(WorldViewProj[eyeIndex], inputPosition).xyww;
	vsout.WorldPosition = mul(World[eyeIndex], inputPosition);
	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], inputPosition);

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

#	if !defined(VR)
cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif

#	include "Common/MotionBlur.hlsli"
#	include "Common/SharedData.hlsli"

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

Texture2D<float> TexDepthSampler : register(t17);

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float3 yyy = Color::Sky(PParams.yyy);
#	if !defined(VR)
	uint eyeIndex = 0;
#	else
	uint eyeIndex = input.EyeIndex;
#	endif  // !VR

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

	// HDR-only sun path: scale glare/disc brightness using user HDR settings.
	if (SharedData::HDRData.x > 0.5 && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun)) {
		// 203 nits is the HDR reference paper white used by the rest of the HDR pipeline.
		const float SUN_REF_PAPER_WHITE_NITS = 203.0;
		float paperWhiteNits = max(SharedData::HDRData.y, 1.0);
		float peakNits = max(SharedData::HDRData.z, paperWhiteNits + 1.0);

		// Peak ratio drives how much brighter the sun can get vs reference HDR white.
		float peakRatio = peakNits / SUN_REF_PAPER_WHITE_NITS;

		// In LL we are already linear; non-LL needs gamma-domain scaling compensation.
		float menuSceneEncoding = SharedData::HDRData.w;
		static const float SUN_DIM_IN_MENU_SCENES = 0.58;  // HDRDisplay::kHdrMenuScenePauseOrMap
		// Dim sun for pause/map/main-menu scenes to avoid UI-facing blowout.
		float hdrSunMenuMul = (menuSceneEncoding > 1e-3) ? SUN_DIM_IN_MENU_SCENES : 1.0;
		float hdrScale = (ENABLE_LL ? peakRatio : pow(peakRatio, rcp(2.2))) * hdrSunMenuMul;

#		if defined(DITHER)
		// DITHER path is the glare sprite-style variant.
		float glareLum = max(Color::RGBToLuminance(baseColor.xyz), 1e-5);

		// Keep glare energy bounded before applying HDR scale.
		if (glareLum > 1.0)
			baseColor.xyz *= rcp(glareLum);

		baseColor.xyz *= hdrScale;

		baseColor.xyz = Color::Sky(input.Color.xyz) * baseColor.xyz;

#			ifdef TEX
		// Fade toward quad edges so the glare sprite blends smoothly.
		float2 glareUv = saturate(input.TexCoord0.xy);
		float glareEdge = min(min(glareUv.x, glareUv.y), min(1.0 - glareUv.x, 1.0 - glareUv.y));
		float glareEdgeFade = smoothstep(0.0, 0.08, glareEdge);
		baseColor.xyz *= glareEdgeFade;
		baseColor.w *= glareEdgeFade;
#			endif

#		else
		// Non-DITHER path is the sun disc/core variant.
		float srcLum = max(Color::RGBToLuminance(baseColor.xyz), 1e-5);

		// Clamp source before boosting the bright disc core.
		if (srcLum > 1.0)
			baseColor.xyz *= rcp(srcLum);

		// Boost the highest-luminance region more strongly than the outer disc.
		float sunCoreBoost = peakRatio;
		float sunCoreMask = smoothstep(0.9, 1.0, saturate(srcLum));
		float discScale = hdrScale * lerp(1.0, sunCoreBoost, sunCoreMask);
		baseColor.xyz *= discScale;

		// Tiny dither to reduce visible banding on bright gradients.
		float ign = Random::InterleavedGradientNoise(floor(input.Position.xy));
		baseColor.xyz += (ign - 0.5) * (discScale / 255.0);

		yyy = 0.0;
#		endif

#		if defined(CLOUD_SHADOWS)
		// Apply cloud transmittance in this same HDR-scaled branch so occlusion remains coherent.
		float3 cloudSampleDir = CloudShadows::GetCloudShadowSampleDir(input.WorldPosition.xyz, SharedData::DirLightDirection.xyz);
		float cloudCube0 = CloudShadows::CloudShadowsTexture.SampleLevel(SampBaseSampler, cloudSampleDir, 0).x;
		float cloudCube1 = CloudShadows::CloudShadowsTexture.SampleLevel(SampBaseSampler, cloudSampleDir, 1).x;
		float cloudCube = lerp(cloudCube0, cloudCube1, 0.5);
		float cloudMult = lerp(1.0, 1.0 - cloudCube, SharedData::cloudShadowsSettings.Opacity);
		float edgeWidth = max(fwidth(cloudMult) * 2.0, 0.02);
		float cloudTransmit = smoothstep(0.12 - edgeWidth, 0.88 + edgeWidth, saturate(cloudMult));
		baseColor.xyz *= cloudTransmit;
		baseColor.w *= cloudTransmit;
#		endif
	}

#		if defined(DITHER)
	float2 noiseGradUv = float2(0.125, 0.125) * input.Position.xy;
	float noiseGrad =
		TexNoiseGradSampler.Sample(SampNoiseGradSampler, noiseGradUv).x * 0.03125 + -0.0078125;

#			ifdef TEX
	float3 sunGlareColor = Color::Sky(input.Color.xyz) * baseColor.xyz;
	if (SharedData::HDRData.x > 0.5 && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun)) {
		// HDR sun block already applied tint/scale; avoid multiplying by tint again.
		sunGlareColor = baseColor.xyz;
	}
	// Dither/noise term is the legacy sky path contribution for gradient smoothing.
	psout.Color.xyz = (sunGlareColor + yyy) + noiseGrad;
	psout.Color.w = baseColor.w * input.Color.w;
#			else
	psout.Color.xyz = (yyy + Color::Sky(input.Color.xyz)) + noiseGrad;
	psout.Color.w = input.Color.w;
#			endif  // TEX

#		elif defined(MOONMASK)
	psout.Color.xyzw = baseColor;

	if (baseColor.w - AlphaTestRefRS.x < 0) {
		discard;
	}

#		elif defined(HORIZFADE)
	psout.Color.xyz = float3(1.5, 1.5, 1.5) * (Color::Sky(input.Color.xyz) * baseColor.xyz + yyy);
	psout.Color.w = input.TexCoord2.x * (baseColor.w * input.Color.w);
#		else  // not DITHER, not MOONMASK, not HORIZFADE
	psout.Color.w = input.Color.w * baseColor.w;
	psout.Color.xyz = Color::Sky(input.Color.xyz) * baseColor.xyz + yyy;
#		endif

#	else
	psout.Color = float4(0, 0, 0, 1.0);
#	endif  // OCCLUSION

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);

	psout.MotionVectors = float4(screenMotionVector, 0, psout.Color.w);
	psout.Normal = float4(0.5, 0.5, 0, psout.Color.w);

#	if defined(CLOUD_SHADOWS) && defined(CLOUDS) && !defined(DEFERRED)
	psout.CloudShadows = float4(1, 1, 1, psout.Color.w);

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
