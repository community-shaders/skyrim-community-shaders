///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation
//
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion",
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// with additional edits by FiveLimbedCat/ProfJack
//
// More references:
//
// Screen Space Indirect Lighting with Visibility Bitmask
//  https://arxiv.org/abs/2301.11376
//
// Exploring Raytraced Future in Metro Exodus
//  https://developer.download.nvidia.com/video/gputechconf/gtc/2019/presentation/s9985-exploring-ray-traced-future-in-metro-exodus.pdf
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Common/Color.hlsli"
#include "Common/FastMath.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "Common/VR.hlsli"
#include "ScreenSpaceGI/common.hlsli"

#define RCP_PI (0.31830988618)

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float3> srcRadiance : register(t2);  // maybe half-res
Texture2D<unorm float2> srcNoise : register(t3);
Texture2D<unorm float> srcAccumFrames : register(t4);  // maybe half-res
Texture2D<float> srcPrevAo : register(t5);             // maybe half-res
Texture2D<float4> srcPrevY : register(t6);             // maybe half-res
Texture2D<float2> srcPrevCoCg : register(t7);          // maybe half-res
Texture2D<float4> srcPrevGISpecular : register(t8);    // maybe half-res

RWTexture2D<unorm float> outAo : register(u0);
RWTexture2D<float4> outY : register(u1);
RWTexture2D<float2> outCoCg : register(u2);
RWTexture2D<float4> outGISpecular : register(u3);
RWTexture2D<half3> outPrevGeo : register(u4);

float GetDepthFade(float depth)
{
	return saturate((depth - DepthFadeRange.x) * DepthFadeScaleConst);
}

// Engine-specific screen & temporal noise loader
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)  // without TAA, temporalIndex is always 0
{
	// noise texture from https://github.com/electronicarts/fastnoise
	// 128x128x64
	uint2 noiseCoord = (pixCoord % 128) + uint2(0, (temporalIndex % 64) * 128);
	return srcNoise.Load(uint3(noiseCoord, 0));
}

// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float GetNormalDistributionFunctionGGX(float roughness, float NdotH)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
	return a2 / (Math::PI * d * d);
}

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float GetVisibilityFunctionSmithJointApprox(float roughness, float NdotV, float NdotL)
{
	float a = roughness * roughness;
	float visSmithV = NdotL * (NdotV * (1 - a) + a);
	float visSmithL = NdotV * (NdotL * (1 - a) + a);
	float vis = visSmithV + visSmithL;
	return vis > 0 ? (0.5 / vis) : 0;
}

void CalculateGI(
	uint2 dtid, float2 uv, float viewspaceZ, float3 viewspaceNormal,
	out float o_ao, out sh2 o_currY, out float2 o_currCoCg, out float4 o_currGIAOSpecular)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	float2 normalizedScreenPos = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	const float rcpNumSlices = rcp((float)NumSlices);
	const float rcpNumSteps = rcp((float)NumSteps);

	// if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance
	const float pixelTooCloseThreshold = 1.3;
	// approx viewspace pixel size at pixCoord; approximation of NDCToViewspace( uv.xy + ViewportSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
	const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * (eyeIndex == 0 ? NDCToViewMul.xy : NDCToViewMul.zw) * RCP_OUT_FRAME_DIM;

	float screenspaceRadius = EffectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;
	screenspaceRadius = max(MinScreenRadius, screenspaceRadius);
	// this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
	const float minS = pixelTooCloseThreshold / screenspaceRadius;

	//////////////////////////////////////////////////////////////////

	const float2 localNoise = SpatioTemporalNoise(dtid, FrameIndex);
	const float noiseSlice = localNoise.x;
	const float noiseStep = localNoise.y;

	//////////////////////////////////////////////////////////////////

	const float3 pixCenterPos = ScreenToViewPosition(normalizedScreenPos, viewspaceZ, eyeIndex);
	const float3 viewVec = normalize(-pixCenterPos);
#ifdef GI_SPECULAR
	const float NoV = clamp(dot(viewVec, viewspaceNormal), 1e-5, 1);
#endif

	// flip foliage normal
	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float visibility = 0;
	float visibilitySpecular = 0;
	float4 radianceY = 0;
	float2 radianceCoCg = 0;
	float3 radianceSpecular = 0;

#ifdef GI_SPECULAR
	const float roughness = max(0.2, saturate(1 - FULLRES_LOAD(srcNormalRoughness, dtid, uv * frameScale, samplerLinearClamp).z));  // can't handle low roughness
#endif

	for (uint slice = 0; slice < NumSlices; slice++) {
		float phi = (Math::PI * rcpNumSlices) * (slice + noiseSlice);
		float3 directionVec = 0;
		sincos(phi, directionVec.y, directionVec.x);

		// convert to px units for later use
		float2 omega = float2(directionVec.x, -directionVec.y) * screenspaceRadius;

		const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
		const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

		float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
		float projectedNormalVecLength = length(projectedNormalVec);
		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float cosNorm = saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength);

		float n = signNorm * FastMath::ACos(cosNorm);

		uint bitmask = 0;
#ifdef GI
		uint bitmaskGI = 0;
#	ifdef GI_SPECULAR
		uint bitmaskGISpecular = 0;
		float3 domVec = getSpecularDominantDirection(viewspaceNormal, viewVec, roughness);
		float3 projectedDomVec = normalize(domVec - axisVec * dot(domVec, axisVec));
		float nDom = sign(dot(orthoDirectionVec, projectedDomVec)) * FastMath::ACos(saturate(dot(projectedDomVec, viewVec)));
#	endif
#endif

		// R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
		float stepNoise = frac(noiseStep + slice * 0.6180339887498948482);

		[unroll] for (int sideSign = -1; sideSign <= 1; sideSign += 2)
		{
			[loop] for (uint step = 0; step < NumSteps; step++)
			{
				float s = (step + stepNoise) * rcpNumSteps;
				s *= s;     // default 2 is fine
				s += minS;  // avoid sampling center pixel

				float2 sampleOffset = s * omega;

				float2 samplePxCoord = dtid + .5 + sampleOffset * sideSign;
				float2 sampleUV = samplePxCoord * RCP_OUT_FRAME_DIM;
				float2 sampleScreenPos = Stereo::ConvertFromStereoUV(sampleUV, eyeIndex);
				[branch] if (any(sampleScreenPos > 1.0) || any(sampleScreenPos < 0.0)) break;

				float sampleOffsetLength = length(sampleOffset);
				float mipLevel = clamp(log2(sampleOffsetLength) - 3.3, 0, 5);
#ifdef HALF_RES
				mipLevel = max(mipLevel, 1);
#elif defined(QUARTER_RES)
				mipLevel = max(mipLevel, 2);
#endif

				float SZ = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel);

				float3 samplePos = ScreenToViewPosition(sampleScreenPos, SZ, eyeIndex);
				float3 sampleDelta = samplePos - pixCenterPos;
				float3 sampleHorizonVec = normalize(sampleDelta);

				float3 sampleBackPos = samplePos - viewVec * Thickness;
				float3 sampleBackHorizonVec = normalize(sampleBackPos - pixCenterPos);

				float angleFront = FastMath::ACos(dot(sampleHorizonVec, viewVec));  // either clamp or use float version for whatever reason
				float angleBack = FastMath::ACos(dot(sampleBackHorizonVec, viewVec));
				float2 angleRange = -sideSign * (sideSign == -1 ? float2(angleFront, angleBack) : float2(angleBack, angleFront));
				// The math: https://www.desmos.com/calculator/je4y5ved2j
				// Using smoothstep for cos: https://discord.com/channels/586242553746030596/586245736413528082/1102228968247144570
				angleRange = smoothstep(0, 1, (angleRange + n) * RCP_PI + .5);

				uint2 bitsRange = uint2(round(angleRange.x * 32u), round((angleRange.y - angleRange.x) * 32u));
				uint maskedBits = s < AORadius ? ((1 << bitsRange.y) - 1) << bitsRange.x : 0;

#ifdef GI
				float3 sampleBackPosGI = samplePos - viewVec * 300;
				float3 sampleBackHorizonVecGI = normalize(sampleBackPosGI - pixCenterPos);
				float angleBackGI = FastMath::ACos(dot(sampleBackHorizonVecGI, viewVec));
				float2 angleRangeGI = -sideSign * (sideSign == -1 ? float2(angleFront, angleBackGI) : float2(angleBackGI, angleFront));

#	ifdef GI_SPECULAR
				float coneHalfAngles = max(5e-2, specularLobeHalfAngle(roughness));  // not too small
				float2 angleRangeSpecular = clamp((angleRangeGI + nDom) * 0.5 / coneHalfAngles, -1, 1) * 0.5 + 0.5;

				uint2 bitsRangeGISpecular = uint2(round(angleRangeSpecular.x * 32u), round((angleRangeSpecular.y - angleRangeSpecular.x) * 32u));
				uint maskedBitsGISpecular = s < GIRadius ? ((1 << bitsRangeGISpecular.y) - 1) << bitsRangeGISpecular.x : 0;
#	endif

				// The math: https://www.desmos.com/calculator/je4y5ved2j
				// Using smoothstep for cos: https://discord.com/channels/586242553746030596/586245736413528082/1102228968247144570
				angleRangeGI = smoothstep(0, 1, (angleRangeGI + n) * RCP_PI + .5);

				uint2 bitsRangeGI = uint2(round(angleRangeGI.x * 32u), round((angleRangeGI.y - angleRangeGI.x) * 32u));
				uint maskedBitsGI = s < GIRadius ? ((1 << bitsRangeGI.y) - 1) << bitsRangeGI.x : 0;

				uint validBits = maskedBitsGI & ~bitmaskGI;
				bool checkGI = validBits;

#	ifdef GI_SPECULAR
				uint overlappedBitsSpecular = maskedBitsGISpecular & ~bitmaskGISpecular;
				checkGI = checkGI || overlappedBitsSpecular;
#	endif

				if (checkGI) {
					float giBoost = 4.0 * Math::PI * (1 + GIDistanceCompensation * smoothstep(0, GICompensationMaxDist, s * EffectRadius));

					// IL
					float3 normalSample = GBuffer::DecodeNormal(srcNormalRoughness.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0).xy);
					if (dot(samplePos, normalSample) > 0)
						normalSample = -normalSample;
					float frontBackMult = -dot(normalSample, sampleHorizonVec);
					frontBackMult = frontBackMult < 0 ? 0.0 : frontBackMult;  // backface

					if (frontBackMult > 0.f) {
						float3 sampleHorizonVecWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], half4(sampleHorizonVec, 0)).xyz);

						float3 sampleRadiance = srcRadiance.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevel).rgb * frontBackMult * giBoost * countbits(validBits) * 0.03125;
						sampleRadiance = max(sampleRadiance, 0);
						float3 sampleRadianceYCoCg = Color::RGBToYCoCg(sampleRadiance);

						radianceY += sampleRadianceYCoCg.r * SphericalHarmonics::Evaluate(sampleHorizonVecWS);
						radianceCoCg += sampleRadianceYCoCg.gb;

#	ifdef GI_SPECULAR
						// thank u Olivier!
						float NoH = clamp(dot(viewspaceNormal, normalize(viewVec + sampleHorizonVec)), 1e-2, 1);
						float NoL = clamp(dot(viewspaceNormal, sampleHorizonVec), 1e-2, 1);

						float3 specularRadiance = sampleRadiance * countbits(overlappedBitsSpecular) * 0.03125;
						specularRadiance *= GetNormalDistributionFunctionGGX(roughness, NoH) * GetVisibilityFunctionSmithJointApprox(roughness, NoV, NoL);
						specularRadiance = max(0, specularRadiance);

						radianceSpecular += specularRadiance;
#	endif
					}
				}
#endif  // GI

				bitmask |= maskedBits;
#ifdef GI
				bitmaskGI |= maskedBitsGI;
#endif
			}
		}

		visibility += countbits(bitmask) * 0.03125;

#if defined(GI) && defined(GI_SPECULAR)
		visibilitySpecular += countbits(bitmaskGISpecular) * 0.03125;
#endif
	}

	float depthFade = GetDepthFade(viewspaceZ);

	visibility *= rcpNumSlices;
	visibility = lerp(saturate(visibility), 0, depthFade);
	visibility = 1 - pow(abs(1 - visibility), AOPower);

#ifdef GI
	radianceY *= rcpNumSlices;
	radianceY = lerp(radianceY, 0, depthFade);

	radianceCoCg *= rcpNumSlices * GISaturation;

#	ifdef GI_SPECULAR
	radianceSpecular *= rcpNumSlices;
	radianceSpecular = lerp(radianceSpecular, 0, depthFade);

	visibilitySpecular *= rcpNumSlices;
	visibilitySpecular = lerp(saturate(visibility), 0, depthFade);
#	endif
#endif

	o_ao = visibility;
	o_currY = radianceY;
	o_currCoCg = radianceCoCg;
	o_currGIAOSpecular = float4(radianceSpecular, visibilitySpecular);
}

[numthreads(8, 8, 1)] void main(const uint2 dtid
								: SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	uint2 pxCoord = dtid;

	float2 uv = (pxCoord + .5) * RCP_OUT_FRAME_DIM;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);

	float2 normalSample = FULLRES_LOAD(srcNormalRoughness, pxCoord, uv * frameScale, samplerLinearClamp).xy;
	float3 viewspaceNormal = GBuffer::DecodeNormal(normalSample);

	half2 encodedWorldNormal = GBuffer::EncodeNormal(ViewToWorldVector(viewspaceNormal, FrameBuffer::CameraViewInverse[eyeIndex]));
	outPrevGeo[pxCoord] = half3(viewspaceZ, encodedWorldNormal);

	// Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
	viewspaceZ *= 0.99920h;  // this is good for FP16 depth buffer

	float currAo = 0;
	float4 currY = 0;
	float2 currCoCg = 0;
	float4 currGIAOSpecular = float4(0, 0, 0, 0);

	bool needGI = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (needGI) {
		CalculateGI(
			pxCoord, uv, viewspaceZ, viewspaceNormal,
			currAo, currY, currCoCg, currGIAOSpecular);

#ifdef TEMPORAL_DENOISER
		float lerpFactor = rcp(srcAccumFrames[pxCoord] * 255);

		currY = lerp(srcPrevY[pxCoord], currY, lerpFactor);
		currCoCg = lerp(srcPrevCoCg[pxCoord], currCoCg, lerpFactor);
#	ifdef GI_SPECULAR
		currGIAOSpecular = lerp(srcPrevGISpecular[pxCoord], currGIAOSpecular, lerpFactor);
#	endif
#endif
	}
	currY = filterNaN(currY);
	currCoCg = filterNaN(currCoCg);
	currGIAOSpecular = filterNaN(currGIAOSpecular);

	outAo[pxCoord] = currAo;
	outY[pxCoord] = currY;
	outCoCg[pxCoord] = currCoCg;
#ifdef GI_SPECULAR
	outGISpecular[pxCoord] = currGIAOSpecular;
#endif
}