// SPDX-License-Identifier: MIT AND (LicenseRef-GPL-3.0-with-Modding-Exception WITH GPL-3.0-linking-source-exception)
// Original code Copyright (c) 2022 Ilya Perapechka (MIT), modifications GPL-3.0 with Modding Exception (see EXCEPTIONS.md)

#include "Common/ColorManagement.hlsli"
#include "Common/FastMath.hlsli"
#include "Common/FlareOcclusion.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#if defined(PROCEDURAL_AURORA)
#	include "ProceduralAurora/ProceduralAurora.hlsli"
#endif

#if defined(PROCEDURAL_SUN)
#	include "ProceduralSun/ProceduralSun.hlsli"

/** @brief True for the in-world sun pass, unless Physical Sky or Effects 11 already draws its own sun. */
bool IsProceduralSunActive()
{
#	if defined(PHYSICAL_SKY)
	if (SharedData::physSkyData.enabled && SharedData::physSkyData.sunDiskCos > 0.0)
		return false;
#	endif
	bool effects11OwnsSun = false;
#	if defined(EFFECTS11)
	effects11OwnsSun = SharedData::enbSettings.EnableProceduralSun != 0;
#	endif
	return SharedData::proceduralSunSettings.enabled && !effects11OwnsSun &&
	       (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun) &&
	       (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld) &&
	       !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
}
#endif

#if defined(VSHADER)
struct VS_INPUT
{
	float4 Position: POSITION0;

#	if defined(TEX) || defined(HORIZFADE)
	float2 TexCoord: TEXCOORD0;
#	endif

	float4 Color: COLOR0;
};
#endif

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

#	if defined(DITHER) && defined(TEX)
/** @brief Fraction of the sun's neighbourhood showing sky, since the vanilla sun query quad sees gaps between distant ridges. */
float GetSunGlareVisibility()
{
	// Scene depth belongs to the main view, not the reflection camera
	if (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection)
		return 1.0;

	float4 sunPositionCS = mul(FrameBuffer::CameraViewProj, float4(SharedData::SunDirection.xyz, 0.0));
	if (sunPositionCS.w <= 0.0)
		return 1.0;

	float2 sunUV = sunPositionCS.xy / sunPositionCS.w * float2(0.5, -0.5) + 0.5;
	uint visibleSamples = 0;
	[unroll] for (uint i = 0; i < FlareOcclusion::SampleCount; i++)
	{
		float2 sampleUV = sunUV + FlareOcclusion::GetSampleOffset(i);
		visibleSamples += FrameBuffer::IsOutsideFrame(sampleUV) || SharedData::GetDepth(sampleUV) >= 1.0;
	}
	return FlareOcclusion::GetVisibility(visibleSamples);
}
#	endif

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 previousInputPosition = inputPosition;

#	if defined(PROCEDURAL_SUN) && defined(TEX) && !defined(DITHER)
	if (IsProceduralSunActive()) {
		float outerCos = SharedData::proceduralSunSettings.haloEnabled && SharedData::proceduralSunSettings.haloIntensity > 0.0f ?
		                     SharedData::proceduralSunSettings.sunHaloCos :
		                     SharedData::proceduralSunSettings.sunDiskCos;
		inputPosition.xyz = StandaloneSun::ResizeBillboardVertex(input.Position.xyz, World, SharedData::proceduralSunSettings.sunQuadModelRadius, outerCos);
		previousInputPosition.xyz = StandaloneSun::ResizeBillboardVertex(input.Position.xyz, PreviousWorld, SharedData::proceduralSunSettings.sunQuadModelRadius, outerCos);
	}
#	endif

#	if defined(PHYSICAL_SKY) && defined(DITHER) && !defined(TEX)
	if (SharedData::physSkyData.enabled) {
		// The atmosphere mesh ends at the equator; the LUT covers the whole sky.
		const float radius = length(inputPosition.xyz);
		const float cosZenith = saturate(inputPosition.z / max(radius, 1e-6));
		inputPosition.xy *= 2.0 * cosZenith;
		inputPosition.z = radius * (2.0 * cosZenith * cosZenith - 1.0);
		previousInputPosition = inputPosition;
	}
#	endif

#	if defined(OCCLUSION)

#		if defined(PROCEDURAL_SUN)
	if (IsProceduralSunActive()) {
		inputPosition.xyz *= StandaloneSun::GetOcclusionBillboardScale(SharedData::proceduralSunSettings.sunQuadModelRadius);
		previousInputPosition = inputPosition;
	}
#		endif

#	elif defined(MOONMASK)

	vsout.TexCoord0 = input.TexCoord;
	vsout.Color = float4(VParams.xxx, 1.0);

#	elif defined(HORIZFADE)

	float worldHeight = mul(World, inputPosition).z;
	float eyeHeightDelta = -EyePosition.z + worldHeight;

	vsout.TexCoord0.xy = input.TexCoord;
	vsout.TexCoord2.x = saturate((1.0 / 17.0) * eyeHeightDelta);
	vsout.Color.xyz = ColorManagement::SRGBToWorking(BlendColor[0].xyz) * VParams;
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

	vsout.Color.xyz = VParams * ColorManagement::SRGBToWorking(skyColor);
	vsout.Color.w = BlendColor[0].w * input.Color.w;
	vsout.SkyBlendColor0 = float4(BlendColor[0].xyz * VParams, 0);
	vsout.SkyBlendColor2 = float4(BlendColor[2].xyz * VParams, 0);
#		if defined(DITHER) && defined(TEX)
	vsout.Color.w *= GetSunGlareVisibility();
#		endif
#	endif  // OCCLUSION MOONMASK HORIZFADE

	vsout.Position = mul(WorldViewProj, inputPosition).xyww;
	vsout.WorldPosition = mul(World, inputPosition);
	vsout.FogPosition = vsout.WorldPosition.xyz - EyePosition.xyz;
	vsout.PreviousWorldPosition = mul(PreviousWorld, previousInputPosition);

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

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

// Sun, moons and stars dimmed and reddened by the atmosphere they are seen through
#	if defined(EFFECTS11) && (defined(HORIZFADE) || (defined(TEX) && !defined(DITHER) && !defined(CLOUDS)))
#		define EFFECTS11_CELESTIAL_EXTINCTION
#	endif

#	if defined(EFFECTS11) && defined(DITHER) && !defined(TEX)
#		define EFFECTS11_SKY_GRADIENT
#	endif

#	if defined(EFFECTS11) && (defined(CLOUDS) || defined(EFFECTS11_CELESTIAL_EXTINCTION) || defined(EFFECTS11_SKY_GRADIENT))
#		include "Effects11/SkyScattering.hlsli"
#	endif

#	ifdef HDR_OUTPUT
#		include "HDRDisplay/HDRSun.hlsli"
#		include "Common/Random.hlsli"
#	endif

#	if defined(PHYSICAL_SKY)
#		define PS_SKY_SAMPLERS
#		include "PhysicalSky/Common.hlsli"
#		if defined(TEX) && defined(CLOUDS)
#			define PS_CLOUDS
#		endif
#	endif

// Effects 11 procedural sun: the disc replaces the sun texture, and clouds in front of it hide it or, when thin, pass it on
#	if defined(EFFECTS11) && defined(TEX) && !defined(MOONMASK) && !defined(DITHER)
#		include "Effects11/ProceduralSun.hlsli"
#		if defined(CLOUDS)
#			define EFFECTS11_CLOUD_SUN
#		else
#			define EFFECTS11_PROCEDURAL_SUN
#		endif
#	endif

#	if defined(EXP_HEIGHT_FOG)
#		define SampColorSampler SampBlendSampler
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

Texture2D<float> TexDepthSampler : register(t17);

#	if defined(PHYSICAL_SKY)
float GetPhysSkyCloudShadow(float3 viewDir, uint2 pxCoord)
{
	if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) != 0) {
#		if defined(CLOUD_SHADOWS)
		float cloudCubeSample = CloudShadows::CloudShadowsTexture.SampleLevel(SampBaseSampler, viewDir, 0).x;
		return saturate(cloudCubeSample * SharedData::cloudShadowsSettings.Opacity);
#		else
		return 0.0;
#		endif
	}

	return PhysSky::GetApShadow(pxCoord);
}
#	endif

#	if defined(EFFECTS11) && defined(HORIZFADE)
/** Stars: StarsCurve and StarsIntensity, plus optional per-star twinkle on isolated bright texels. */
float3 ShadeStars(float4 starTexel, float2 uv)
{
	float3 color = starTexel.xyz;
	[branch] if (SharedData::enbSettings.EnableAnimatedStars)
	{
		float2 textureSize;
		TexBaseSampler.GetDimensions(textureSize.x, textureSize.y);
		uint seed = Random::iqint3(uint2(floor(frac(uv) * textureSize)));
		float2 star = Random::f2(seed);
		float4 quad = TexBaseSampler.GatherAlpha(SampBaseSampler, uv);
		float isolation = max(max(quad.x, quad.y), max(quad.z, quad.w)) - dot(quad, 0.25);
		float mask = saturate(isolation * SharedData::enbSettings.StarsAnimationDensity - 0.5);
		float rate = SharedData::enbSettings.StarsAnimationTime * (2.0 + 6.0 * star.y);
		float wave = 0.5 + 0.5 * sin(Math::TAU * frac(SharedData::Timer * rate + star.x));
		color *= 1.0 + wave * wave * mask * SharedData::enbSettings.StarsAnimationIntensity * 0.3;
	}
	float3 squared = color * color;
	color = lerp(color, squared * squared, SharedData::enbSettings.StarsCurve);
	return max(color, 0.0) * SharedData::enbSettings.StarsIntensity;
}
#	endif

#	if defined(EFFECTS11_PROCEDURAL_SUN) || defined(EFFECTS11_CLOUD_SUN)
/**
 * @brief Procedural sun disc and halo toward viewDirection, before the sun tint multiplies it.
 * @param sunTint The tint applied afterwards; the disc is scaled so that tint times disc carries the sunlight (L = E / solid angle).
 * @param haloIntensity Halo brightness; 0 leaves only the disc.
 */
void EvaluateProceduralSun(float3 viewDirection, float3 sunTint, float haloIntensity, out float3 color, out float coverage)
{
	color = 0.0;
	coverage = 0.0;

	float cosTheta = clamp(dot(viewDirection, SharedData::SunDirection.xyz), -1.0, 1.0);
	float influenceCos = ProceduralSun::GetInfluenceCos(
		SharedData::enbSettings.ProceduralSunDiskCos,
		true,
		SharedData::enbSettings.ProceduralSunHaloCos,
		haloIntensity);
	[branch] if (cosTheta <= influenceCos) return;

	float discRadiance = SharedData::enbSettings.ProceduralSunDiskRadiance * Color::RGBToLuminance(SharedData::DirLightColor.xyz) /
	                     max(Color::RGBToLuminance(sunTint), 1e-5);

	SunDisc::Evaluate(
		cosTheta,
		SharedData::enbSettings.ProceduralSunDiskCos,
		SharedData::enbSettings.ProceduralSunEdgeSoftness,
		SharedData::enbSettings.ProceduralSunHaloCos,
		SharedData::enbSettings.ProceduralSunHaloFalloff,
		discRadiance,
		haloIntensity,
		color,
		coverage);
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
#	if defined(EFFECTS11)
	float3 skyScale = ColorManagement::SRGBToWorking(PParams.yyy * SharedData::enbSettings.SkyScaleMultiplier);
#	else
	float3 skyScale = ColorManagement::SRGBToWorking(PParams.yyy);
#	endif
	float alphaTransmittance = 1.0;
#	if defined(PROCEDURAL_AURORA) && defined(DITHER) && !defined(TEX)
	float3 auroraColor = 0.0;  // kept for the physical sky dome, which replaces the vanilla gradient
#	endif

#	if defined(PS_CLOUDS)
	float psCloudDist = 1e3f / 1.428e-2;
	float3 viewDir = normalize(input.WorldPosition.xyz);
#		if defined(CLOUD_SHADOWS)
	if (SharedData::physSkyData.enabled)
		psCloudDist = CloudShadows::IntersectCloudDist(float3(0, 0, 0), viewDir);
#		endif
#	endif

#	ifndef OCCLUSION
#		ifndef TEXLERP
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
#			if defined(EFFECTS11) && defined(HORIZFADE)
	[branch] if (SharedData::enbSettings.Enable)
		baseColor.xyz = ShadeStars(baseColor, input.TexCoord0.xy);
#			elif defined(EFFECTS11) && defined(TEX) && !defined(DITHER) && !defined(CLOUDS) && !defined(MOONMASK)
	[branch] if (SharedData::enbSettings.Enable && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsMoon))
	{
		float2 edge = abs(input.TexCoord0.xy * 2.0 - 1.0);
		baseColor.xyz = pow(max(baseColor.xyz, 0.0), SharedData::enbSettings.MoonCurve);
		// Drop the billboard's border texels so the moon texture cannot bleed at its edges
		if (max(edge.x, edge.y) > 0.985)
			baseColor = 0.0;
	}
#			endif
#			ifndef MOONMASK
	baseColor.xyz = ColorManagement::TextureToWorking(baseColor.xyz);
#			endif
#			ifdef TEXFADE
	baseColor.w *= PParams.x;
#			endif
#		else
	float4 blendColor = TexBlendSampler.Sample(SampBlendSampler, input.TexCoord1.xy);
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	blendColor.xyz = ColorManagement::TextureToWorking(blendColor.xyz);
	baseColor.xyz = ColorManagement::TextureToWorking(baseColor.xyz);
	baseColor = PParams.xxxx * (-baseColor + blendColor) + baseColor;
#		endif
#		if defined(PHYSICAL_SKY)
	bool enableProceduralSun = SharedData::physSkyData.sunDiskCos > 0.0 && SharedData::physSkyData.enabled && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	baseColor.xyz *= enableProceduralSun ? 0.f : 1.f;
#		else
	bool enableProceduralSun = false;
#		endif

	float2 hdrSunTexCoord = input.TexCoord0.xy;
	float sunCloudTransmission = 1.0;
	bool standaloneSunActive = false;
#		if defined(PROCEDURAL_SUN) && defined(TEX) && !defined(DITHER)
	standaloneSunActive = IsProceduralSunActive();
	if (standaloneSunActive) {
		float3 viewDirection = normalize(input.WorldPosition.xyz);
		float cosTheta = clamp(dot(viewDirection, SharedData::SunDirection.xyz), -1.0f, 1.0f);
		float3 proceduralSunColor;
		float sunCoverage;
		SunDisc::Evaluate(
			cosTheta,
			SharedData::proceduralSunSettings.sunDiskCos,
			SharedData::proceduralSunSettings.edgeSoftness,
			SharedData::proceduralSunSettings.sunHaloCos,
			SharedData::proceduralSunSettings.haloFalloff,
			SharedData::proceduralSunSettings.diskIntensity,
			SharedData::proceduralSunSettings.haloEnabled ? SharedData::proceduralSunSettings.haloIntensity : 0.0f,
			proceduralSunColor,
			sunCoverage);

		baseColor.xyz = ColorManagement::TextureToWorking(proceduralSunColor, true);
		baseColor.w = sunCoverage;
#			if defined(CLOUD_SHADOWS)
		if (sunCoverage > 0.0f && SharedData::proceduralSunSettings.cloudOcclusionStrength > 0.0f) {
			float cloudOpacity = CloudShadows::CloudShadowsTexture.SampleLevel(SampBaseSampler, viewDirection, 0).x;
			sunCloudTransmission = StandaloneSun::GetCloudTransmission(cloudOpacity, SharedData::proceduralSunSettings.cloudOcclusionStrength);
		}
#			endif
		// The resized billboard's UVs span the outer cone, not the disc, so the HDR gain weighs only brightness and coverage
		hdrSunTexCoord = 0.5;
		skyScale = 0.0;
	}
#		endif

#		if defined(EFFECTS11_PROCEDURAL_SUN)
	bool proceduralSunActive = !enableProceduralSun && SharedData::enbSettings.EnableProceduralSun && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	[branch] if (proceduralSunActive)
	{
		float3 viewDirection = normalize(input.WorldPosition.xyz);
		float3 proceduralSunColor;
		float sunCoverage;
		EvaluateProceduralSun(viewDirection, input.Color.xyz, SharedData::enbSettings.ProceduralSunHaloIntensity, proceduralSunColor, sunCoverage);

#			if defined(CLOUD_SHADOWS)
		// The clouds drawn next hide the sun only as much as the sky; dimming it here by their total opacity makes them hide it harder
		float cloudOpacity = saturate(CloudShadows::CloudShadowsTexture.SampleLevel(SampBaseSampler, viewDirection, 0));
		proceduralSunColor *= pow(1.0 - cloudOpacity, (ProceduralSun::CloudExtinction - 1.0) * cloudOpacity);
#			endif

		// A sun texture that is transparent at its center hides the sun, so it hides the disc too
		float sunTextureAlpha = saturate(TexBaseSampler.SampleLevel(SampBaseSampler, 0.5, 0).w);

		baseColor.xyz = proceduralSunColor;
		baseColor.w = sunCoverage * sunTextureAlpha;

		skyScale = 0.0;
	}
#		endif

#		if defined(HDR_OUTPUT)
#			if defined(EFFECTS11_PROCEDURAL_SUN)
	[branch] if (!proceduralSunActive)
	{
#			endif
		float hdrSunGain = HDRSun::GetHdrSunGain(hdrSunTexCoord, baseColor);
		baseColor.xyz *= hdrSunGain;
		if (HDRSun::IsHdrSunActive() && !enableProceduralSun) {
			// Dither bright output to reduce banding in high-boost sun path.
			// Same baseColor/skyScale treatment for DITHER and non-DITHER; DITHER adds noiseGrad later.
			baseColor.xyz += (Random::InterleavedGradientNoise(input.Position.xy) - 0.5f) *
			                 (saturate(hdrSunGain - 1.0f) / 255.0f);
			skyScale = 0.0f;
		}

#			if defined(CLOUD_SHADOWS)
		if (HDRSun::IsHdrSunActive() && !enableProceduralSun && !standaloneSunActive) {
			float cloudMult = CloudShadows::GetCloudShadowMult(input.WorldPosition.xyz, SampBaseSampler);
			baseColor.xyz *= cloudMult;
#				if defined(ENABLE_LL)
			alphaTransmittance = cloudMult;
#				else
			baseColor.w *= cloudMult;
#				endif
		}
#			endif
#			if defined(EFFECTS11_PROCEDURAL_SUN)
	}
#			endif
#		endif

#		if defined(PS_CLOUDS) && defined(CLOUD_SHADOWS)
	if (SharedData::physSkyData.enabled && SharedData::physSkyData.enableVanillaClouds)
		baseColor.rgb = PhysSky::RelightCloud(baseColor, viewDir, float3(0, 0, 0) + viewDir * psCloudDist, PhysSky::SampTr, SampBaseSampler);
	else if (SharedData::physSkyData.enabled && !SharedData::physSkyData.enableVanillaClouds)
		baseColor.a = 0;  // Hide vanilla clouds when disabled
#		elif defined(PS_CLOUDS)
	if (SharedData::physSkyData.enabled && !SharedData::physSkyData.enableVanillaClouds)
		baseColor.a = 0;  // Hide vanilla clouds when disabled (no cloud shadows)
#		endif
	// After the HDR gain, whose coverage weighting would otherwise count the cloud a second time
	baseColor.w *= sunCloudTransmission;

#		if defined(DITHER)
	float2 noiseGradUv = float2(0.125, 0.125) * (input.Position.xy + Random::TemporalTileOffset(SharedData::FrameCount, 8.0));
	float noiseGrad = TexNoiseGradSampler.Sample(SampNoiseGradSampler, noiseGradUv).x * 0.03125 - 0.0078125;
	noiseGrad *= 10.0;

#			ifdef TEX
	psout.Color.xyz = input.Color.xyz * baseColor.xyz + skyScale;
	psout.Color.xyz *= 1.0 + noiseGrad;
	psout.Color.w = baseColor.w * input.Color.w;
#			else
	float3 skyGradientColor = input.Color.xyz;

#				if defined(EFFECTS11)
	float3 viewDirection = normalize(input.WorldPosition.xyz);
	if (SharedData::enbSettings.UseProceduralGradientWeights) {
		float gradientPosition = pow(1.0 - saturate(viewDirection.z), SharedData::enbSettings.ProceduralGradientWeightCurve);
		skyGradientColor = lerp(input.SkyBlendColor2.xyz, input.SkyBlendColor0.xyz, gradientPosition);
	}
	[branch] if (SharedData::enbSettings.EnableCloudsScattering)
		skyGradientColor = SkyScattering::ApplySkyScattering(skyGradientColor, input.SkyBlendColor2.xyz, viewDirection) + SkyScattering::GetMoonGlow(viewDirection);
#				endif
	psout.Color.xyz = skyGradientColor + skyScale;

#				if defined(PROCEDURAL_AURORA)
	// Drawn on the sky dome so stars, moons and clouds still draw over it. The gradient is taken outside the branch.
	const float3 auroraDirection = normalize(input.WorldPosition.xyz);
	const float auroraPixelAngle = length(fwidth(auroraDirection));
	[branch] if (SharedData::proceduralAuroraSettings.Enabled && SharedData::proceduralAuroraSettings.Atmosphere.x > 0.0 && auroraDirection.z > -0.03)
	{
		auroraColor = ColorManagement::SRGBToWorking(ProceduralAurora::Render(auroraDirection, auroraPixelAngle));
#					if defined(EFFECTS11)
		[branch] if (SharedData::enbSettings.Enable)
			auroraColor = pow(max(auroraColor, 0.0), SharedData::enbSettings.AuroraCurve) * SharedData::enbSettings.AuroraIntensity;
#					endif
		psout.Color.xyz += auroraColor;
	}
#				endif

#				if defined(EFFECTS11)
	// FixBlackCrush: multiplicative dither vanishes near black and bands dark skies, so fade to additive dither there
	[branch] if (SharedData::enbSettings.Enable && SharedData::enbSettings.FixBlackCrush)
	{
		float3 additiveDither = max(psout.Color.xyz + noiseGrad * 0.1, 0.0);
		psout.Color.xyz = lerp(additiveDither, psout.Color.xyz * (1.0 + noiseGrad), saturate(dot(psout.Color.xyz, 8.0)));
	}
	else
	{
		psout.Color.xyz *= 1.0 + noiseGrad;
	}
#				else
	psout.Color.xyz *= 1.0 + noiseGrad;
#				endif
	psout.Color.w = input.Color.w;
#			endif  // TEX

#		elif defined(MOONMASK)
	psout.Color.xyzw = baseColor;

	if (baseColor.w - AlphaTestRefRS.x < 0) {
		discard;
	}

#		elif defined(HORIZFADE)
#			if defined(EFFECTS11)
	// StarsIntensity owns star brightness, so the sky's additive scale must not lift the star layer
	if (SharedData::enbSettings.Enable)
		skyScale = 0.0;
#			endif
	psout.Color.xyz = float3(1.5, 1.5, 1.5) * (input.Color.xyz * baseColor.xyz + skyScale);
	psout.Color.w = input.TexCoord2.x * (baseColor.w * input.Color.w);
#		else

	psout.Color.w = input.Color.w * baseColor.w;
	psout.Color.xyz = input.Color.xyz * baseColor.xyz + skyScale;

#			if defined(EFFECTS11_PROCEDURAL_SUN)
	[branch] if (proceduralSunActive)
		psout.Color = ProceduralSun::ToAdditiveBlend(psout.Color, SharedData::enbSettings.ProceduralSunRadianceLimit);
#			endif

#			if defined(CLOUDS) && defined(EFFECTS11)
	[branch] if (SharedData::enbSettings.Enable)
	{
		psout.Color.w = saturate(input.Color.w * baseColor.w * (1.0 + baseColor.w * SharedData::enbSettings.CloudsVertexAlphaBoost));
		[branch] if (psout.Color.w > 0.0)
		{
			float3 viewDirection = normalize(input.WorldPosition.xyz);
			float cloudTextureAlpha = saturate(baseColor.w);
			float cloudTextureBrightness = max(dot(baseColor.xyz, 1.0 / 3.0), 0.0);
			float cloudTextureGray = pow(cloudTextureBrightness, SharedData::enbSettings.CloudsCurve);

			float3 cloudColor = pow(max(input.Color.xyz * baseColor.xyz, 0.0), SharedData::enbSettings.CloudsCurve);
			cloudColor = lerp(cloudColor, dot(cloudColor, 1.0 / 3.0), SharedData::enbSettings.CloudsDesaturation) * SharedData::enbSettings.CloudsIntensity * SharedData::enbSettings.CloudsColorFilter;

			bool cloudScattering = SharedData::enbSettings.EnableCloudsScattering && !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::NoSkyScattering);
			psout.Color.xyz = SkyScattering::ShadeCloud(cloudColor, cloudTextureAlpha, cloudTextureGray, cloudTextureBrightness, viewDirection, cloudScattering) + skyScale * min(SharedData::enbSettings.CloudsIntensity, 1.0);

#				if defined(EFFECTS11_CLOUD_SUN)
			[branch] if (SharedData::enbSettings.EnableProceduralSun && SharedData::SunColor.w > 0.0)
			{
				float3 sunTint = SharedData::SunColor.xyz / SharedData::SunColor.w;
				float3 sunColor;
				float sunCoverage;
				EvaluateProceduralSun(viewDirection, sunTint, SharedData::enbSettings.ProceduralSunHaloIntensity, sunColor, sunCoverage);
				[branch] if (sunCoverage > 0.0)
				{
					float sunBehind = Color::RGBToLuminance(sunColor * sunCoverage * SharedData::SunColor.xyz);
#					if !defined(CLOUD_SHADOWS)
					// Without cloud occlusion the sun cannot dim itself, so where it outshines the cloud a deeper cloud hides its leak;
					// faint layers barely deepen, so layers stacked over a cloud that already hid the sun add little glow
					float sunContrast = saturate(sunBehind / max(ProceduralSun::CloudSunContrast * Color::RGBToLuminance(psout.Color.xyz), 1e-5) - 1.0);
					float cloudAlpha = psout.Color.w;
					psout.Color.w = 1.0 - pow(1.0 - cloudAlpha, lerp(1.0, ProceduralSun::CloudExtinction, sunContrast * cloudAlpha));

					// The halo the deeper cloud hides comes back as a smooth glow, so clouds around the sun stay lit without its sharp rim
					float haloPlateau = ProceduralSun::EvaluateHaloPlateau(
						dot(viewDirection, SharedData::SunDirection.xyz),
						SharedData::enbSettings.ProceduralSunDiskCos,
						SharedData::enbSettings.ProceduralSunHaloCos,
						SharedData::enbSettings.ProceduralSunHaloFalloff);
					psout.Color.xyz += (psout.Color.w - cloudAlpha) / psout.Color.w * SharedData::enbSettings.ProceduralSunHaloIntensity * haloPlateau * SharedData::SunColor.xyz;
#					endif

					// Thin texture passes the disc on through alpha, which cannot show a sun an earlier layer already hid
					float3 discColor;
					float discCoverage;
					EvaluateProceduralSun(viewDirection, sunTint, 0.0, discColor, discCoverage);
					float discShare = saturate(Color::RGBToLuminance(discColor * discCoverage * SharedData::SunColor.xyz) / max(sunBehind, 1e-5));
					float cloudClear = 1.0 - cloudTextureAlpha;
					psout.Color.w *= 1.0 - cloudClear * cloudClear * cloudClear * discShare;
				}
			}
#				endif
		}
	}
#			endif
#		endif

#		if defined(EFFECTS11_CELESTIAL_EXTINCTION)
	[branch] if (SharedData::enbSettings.EnableCloudsScattering && !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun))
		psout.Color *= SkyScattering::GetCelestialExtinction(normalize(input.WorldPosition.xyz));
#		endif

#	else
	psout.Color = float4(0, 0, 0, 1.0);
#	endif  // OCCLUSION

	const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) != 0;
#	if defined(PHYSICAL_SKY)
	if (SharedData::physSkyData.enabled) {
#		if defined(DITHER) && !defined(TEX)
		// SKY
		float3 skyViewDir = normalize(input.WorldPosition.xyz);
		float skyShadow = GetPhysSkyCloudShadow(skyViewDir, input.Position.xy);
		float2 physSkyScreenUV = input.Position.xy * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res
		float3 physSkyColor = inReflection ? PhysSky::SampleSky(skyViewDir, skyShadow, PhysSky::SampSv) : PhysSky::SampleViewSky(skyViewDir, skyShadow, PhysSky::SampSv);
#			if defined(PROCEDURAL_AURORA)
		// Like stars and moons: attenuated by the atmosphere and added before volumetric clouds cover it
		physSkyColor += auroraColor * PhysSky::SampleViewCelestialTr(skyViewDir, PhysSky::SampSv);
#			endif
		float3 skyColor = physSkyColor;
		if (inReflection && ViewAtmosphere::ReflectionEnabled()) {
			uint tailWidth, tailHeight;
			PhysSky::TexSkyTail.GetDimensions(tailWidth, tailHeight);
			const float3 tail = PhysSky::TexSkyTail.SampleLevel(PhysSky::SampSv, PhysSky::SkyViewLutUv(skyViewDir, tailHeight), 0).rgb;
			skyColor = PhysSky::CompositeReflectionMedium(tail, skyViewDir, PhysSky::AP_MAX_DIST, PhysSky::SampSv, true);
		} else if (SharedData::physSkyData.enableVolumetricClouds && (inReflection || !SharedData::PostWaterComposite))
			skyColor = inReflection ? PhysSky::CompositeVolumetricCloudsCube(physSkyColor, skyViewDir, PhysSky::SampSv) : PhysSky::CompositeVolumetricCloudsUv(physSkyColor, physSkyScreenUV, PhysSky::SampSv);
		psout.Color = lerp(float4(skyColor, 1.0f), psout.Color, SharedData::physSkyData.vanillaMix);

#		elif defined(PS_CLOUDS)
		float apShadow = GetPhysSkyCloudShadow(viewDir, input.Position.xy);
		float4 apColor = PhysSky::SampleAp(viewDir, psCloudDist, apShadow, PhysSky::SampSv);
		if (inReflection && ViewAtmosphere::ReflectionEnabled()) {
			const ViewAtmosphere::Medium medium = ViewAtmosphere::SampleReflection(PhysSky::SkyViewLutUv(viewDir, 32u), psCloudDist, PhysSky::SampSv);
			psout.Color.xyz = ColorManagement::SceneColor::ScaleAndAddLinear(psout.Color.xyz, medium.transmittance, medium.luminance);
		} else
			psout.Color.xyz = psout.Color.xyz * apColor.a + apColor.rgb;
#		elif defined(TEX) && defined(DEFERRED)
		float3 sunDir = normalize(SharedData::physSkyData.sunDir);
		float cosTheta = saturate(dot(normalize(input.WorldPosition.xyz), sunDir));
		if (enableProceduralSun && cosTheta > SharedData::physSkyData.sunDiskCos && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld)) {
			float sunDiskSin = sqrt(1.0 - SharedData::physSkyData.sunDiskCos * SharedData::physSkyData.sunDiskCos);
			float tanTheta = sqrt(1.0 - cosTheta * cosTheta) / cosTheta;
			float normDist = tanTheta * SharedData::physSkyData.sunDiskCos * rcp(sunDiskSin);
			float3 limbFactor = Color::LinearSRGBToWorking(PhysSky::LimbDarkenHestroffer(normDist));

			const float softEdge = saturate(8.0f * (cosTheta - SharedData::physSkyData.sunDiskCos) / (1.0f - SharedData::physSkyData.sunDiskCos));
			const float sunSolidAngle = Math::TAU * (1.0f - SharedData::physSkyData.sunDiskCos);
			const float3 transmittance = PhysSky::SampleViewCelestialTr(normalize(input.WorldPosition.xyz), SampBlendSampler);
			float3 sunDiskRadiance = (SharedData::physSkyData.sunlightColor / max(sunSolidAngle, 1e-6f)) * transmittance;
#			if !defined(ENABLE_LL)
			sunDiskRadiance = min(sunDiskRadiance, 62250.0f);
#			endif

			float3 sunDiskColor = sunDiskRadiance * limbFactor * softEdge;
			psout.Color.xyz = sunDiskColor;
			psout.Color.w = 1.0;
		} else if (enableProceduralSun) {
			psout.Color = 0.0f;
		} else {
			psout.Color.xyz *= PhysSky::SampleViewCelestialTr(normalize(input.WorldPosition.xyz), PhysSky::SampSv);
		}
		if (SharedData::physSkyData.enableVolumetricClouds && (inReflection || !SharedData::PostWaterComposite)) {
			float2 physSkyScreenUV = input.Position.xy * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res
			psout.Color.xyz = inReflection ? ColorManagement::SceneColor::ScaleByLinear(psout.Color.xyz, PhysSky::TexVolCubeTr.SampleLevel(PhysSky::SampSv, normalize(input.WorldPosition.xyz), 0)) : PhysSky::ApplyVolumetricCloudTransmittanceUv(psout.Color.xyz, physSkyScreenUV, PhysSky::SampSv);
		}
#		else
#			if defined(HORIZFADE) || (defined(TEX) && !defined(MOONMASK))
		psout.Color.xyz *= PhysSky::SampleViewCelestialTr(normalize(input.WorldPosition.xyz), PhysSky::SampSv);
#			endif
#			ifndef OCCLUSION
		if (enableProceduralSun) {
			psout.Color = 0.0f;
		} else
#			endif
			if (SharedData::physSkyData.enableVolumetricClouds && (inReflection || !SharedData::PostWaterComposite)) {
			float2 physSkyScreenUV = input.Position.xy * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res
			float3 physSkyViewDir = normalize(input.WorldPosition.xyz);
			psout.Color.xyz = inReflection ? PhysSky::CompositeVolumetricCloudsCube(psout.Color.xyz, physSkyViewDir, PhysSky::SampSv) : PhysSky::CompositeVolumetricCloudsUv(psout.Color.xyz, physSkyScreenUV, PhysSky::SampSv);
		}
#		endif
	}
#	endif

#	if defined(EXP_HEIGHT_FOG)
	if (inReflection && SharedData::exponentialHeightFogSettings.enabled) {
		float skyFogDistance = SharedData::CameraData.x;
#		if defined(HORIZON_FIX)
		// Match the main view (ISSAOComposite.hlsl): fog the sky out to the HorizonFix far water's horizon
		skyFogDistance = max(skyFogDistance, SharedData::horizonFixSettings.farWaterDistance);
#		endif
		float3 skyFogPosition = normalize(input.FogPosition.xyz) * skyFogDistance;
		float4 exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFogNoVolumetric(skyFogPosition, FrameBuffer::CameraPosAdjust.xyz, psout.Color.xyz, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		psout.Color.xyz = lerp(psout.Color.xyz, exponentialHeightFog.xyz, exponentialHeightFog.w);
	}
#	endif

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	psout.MotionVectors = float4(screenMotionVector, 0, psout.Color.w * alphaTransmittance);
	psout.Normal = float4(0.5, 0.5, 0, psout.Color.w * alphaTransmittance);

#	if defined(CLOUD_SHADOWS) && defined(CLOUDS) && !defined(DEFERRED)
	// A broadcast alpha would square coverage under SRC_ALPHA blending.
	psout.CloudShadows = float4(1, 1, 1, psout.Color.w * alphaTransmittance);

	// Keep sun behind scene depth to prevent halo leaks through geometry.
	float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
	if (depth < input.Position.z)
		psout.Color.w = 0;

#	elif !defined(DITHER) || !defined(TEX)
	// Even without cloud shadows enabled, sun disc should be occluded by scene depth (clouds, terrain, etc.)
	// The sun glare pass (DITHER + TEX) is skipped: it fades by depth coverage in the VS instead,
	// and the per-pixel reject made the glare disappear. Reflections skip it: t17 does not hold the reflection face's depth.
	[branch] if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun) && !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) && psout.Color.w > 0.0)
	{
		float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
		if (depth < input.Position.z)
			psout.Color.w = 0;
	}
#	endif

#	if defined(ENABLE_LL)
#		if (defined(TEX) || defined(HORIZFADE)) && !defined(MOONMASK) && !defined(CLOUDS)
	if ((!Permutation::RenderToUI || inReflection) && !SharedData::physSkyData.enabled) {
		psout.Color.w = pow(saturate(psout.Color.w), TransferFunctions::GAME_GAMMA);
	}
#		endif
	if (Permutation::RenderToUI && !inReflection)
		psout.Color.rgb = ColorManagement::WorkingToUI(psout.Color.rgb);
#	endif
	psout.Color.w *= alphaTransmittance;
#	if !defined(OCCLUSION)
	psout.Color.rgb = PreExposure::EncodeScene(psout.Color.rgb);
#	endif
	return psout;
}
#endif
