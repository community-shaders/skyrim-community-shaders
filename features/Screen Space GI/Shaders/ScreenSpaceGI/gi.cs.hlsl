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
#include "Common/VR.hlsli"
#include "NRD/NRDReblurSH.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float3> srcRadiance : register(t2);
Texture2D<unorm float2> srcNoise : register(t3);
#if defined(DYNAMIC_CUBEMAPS)
TextureCube<float3> EnvTexture : register(t4);
TextureCube<float3> EnvReflectionsTexture : register(t5);
#	if defined(SKYLIGHTING)
#		define SKYLIGHTING_PROBE_REGISTER t6
#		include "Skylighting/Skylighting.hlsli"
#	endif
#endif
Texture2D<float2> srcNormal : register(t8);

RWTexture2D<float4> outRadianceHitDist : register(u0);
RWTexture2D<half3> outPrevGeo : register(u1);
#ifdef SSGI_SH
RWTexture2D<float4> outSH1 : register(u2);
#endif

// Engine-specific screen & temporal noise loader
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)  // without TAA, temporalIndex is always 0
{
	// noise texture from https://github.com/electronicarts/fastnoise
	// 128x128x64
	uint2 noiseCoord = (pixCoord % 128) + uint2(0, (temporalIndex % 64) * 128);
	return srcNoise.Load(uint3(noiseCoord, 0));
}

void CalculateGI(
	uint2 dtid, float2 uv, float viewspaceZ, float3 viewspaceNormal,
	out float o_ao, out float3 o_radiance
#ifdef SSGI_SH
	,
	out float3 o_direction
#endif
)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	float2 normalizedScreenPos = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	const float rcpNumSteps = rcp((float)NumSteps);

	//////////////////////////////////////////////////////////////////

	// Use mono screen-space position for noise indexing so both eyes
	// sample the same noise for corresponding world positions.
	uint2 noiseCoord = uint2(normalizedScreenPos * OUT_FRAME_DIM);
	const float2 localNoise = SpatioTemporalNoise(noiseCoord, FrameIndex);
	const float noiseSlice = localNoise.x;
	const float noiseStep = localNoise.y;

	//////////////////////////////////////////////////////////////////

	const float3 pixCenterPos = ScreenToViewPosition(normalizedScreenPos, viewspaceZ, eyeIndex);
	const float3 viewVec = normalize(-pixCenterPos);

	// flip foliage normal
	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float visibility = 0;
	float3 totalRadiance = 0;
#ifdef SSGI_SH
	float3 totalDirection = 0;
#endif

	{
		float phi = Math::PI * noiseSlice;
		float3 directionVec = 0;
		sincos(phi, directionVec.y, directionVec.x);

		float2 omega_dir = float2(directionVec.x, -directionVec.y);
		float2 pixPos = dtid + 0.5;
		float2 absDir = max(abs(omega_dir), 1e-6);

		const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
		const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

		float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
		// 1/length(v) == rsqrt(dot(v,v)). max() guards against a zero-length projection.
		float rcpProjectedNormalVecLength = rsqrt(max(dot(projectedNormalVec, projectedNormalVec), EPSILON_LENGTH_SQ));
		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcpProjectedNormalVecLength);

		float n = signNorm * FastMath::ACos(cosNorm);

		uint bitmask = 0;
#ifdef GI
		uint bitmaskGI = 0;
#endif

		float stepNoise = noiseStep;

		[unroll] for (int sideSign = -1; sideSign <= 1; sideSign += 2)
		{
			float2 sideDir = omega_dir * sideSign;
			float2 edgeDist;
			edgeDist.x = sideDir.x >= 0 ? (OUT_FRAME_DIM.x - pixPos.x) : pixPos.x;
			edgeDist.y = sideDir.y >= 0 ? (OUT_FRAME_DIM.y - pixPos.y) : pixPos.y;
			float screenspaceRadius = min(edgeDist.x / absDir.x, edgeDist.y / absDir.y);

			float2 omega = omega_dir * screenspaceRadius;
			float logLenOmega = 0.5 * log2(max(dot(omega, omega), EPSILON_LENGTH_SQ));

			[loop] for (uint step = 0; step < NumSteps; step++)
			{
				float s = (step + stepNoise) * rcpNumSteps;
				s *= s;

				float2 sampleOffset = s * omega;

				float2 samplePxCoord = dtid + .5 + sampleOffset * sideSign;
				float2 sampleUV = samplePxCoord * RCP_OUT_FRAME_DIM;

				uint sampleEyeIndex = Stereo::GetEyeIndexFromTexCoord(sampleUV);
				float2 sampleScreenPos = Stereo::ConvertFromStereoUV(sampleUV, sampleEyeIndex);
				[branch] if (any(sampleScreenPos > 1.0) || any(sampleScreenPos < 0.0)) continue;

				float mipLevel = clamp(log2(s) + logLenOmega - 3.3, 0, 5);
				float mipLevelRadiance = max(mipLevel, 1);

				float SZ = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel);

				// Reconstruct sample in current eye's viewspace for correct horizon angles.
				float3 samplePos = ScreenToViewPosition(sampleScreenPos, SZ, sampleEyeIndex);
				// For cross-eye samples, reject if the depth differs too much from the
				// center pixel -- the other eye may see a different surface due to occlusion.
#if defined(VR)
				if (sampleEyeIndex != eyeIndex) {
					if (abs(SZ - viewspaceZ) > viewspaceZ * 0.1)
						continue;
					samplePos = FrameBuffer::WorldToView(FrameBuffer::ViewToWorld(samplePos, true, sampleEyeIndex), true, eyeIndex);
				}
#endif
				float3 sampleDelta = samplePos - pixCenterPos;
				float3 sampleHorizonVec = normalize(sampleDelta);

				// Back-side horizon vector for the surface-thickness offset.
				float3 sampleBackHorizonVec = normalize(sampleDelta - viewVec * Thickness * viewspaceZ);

				float angleFront = FastMath::ACos(dot(sampleHorizonVec, viewVec));
				float angleBack = FastMath::ACos(dot(sampleBackHorizonVec, viewVec));
				float2 angleRange = -sideSign * (sideSign == -1 ? float2(angleFront, angleBack) : float2(angleBack, angleFront));
				angleRange = smoothstep(0, 1, (angleRange + n) * Math::INV_PI + .5);

				uint2 bitsRange = uint2(round(angleRange.x * 32u), round((angleRange.y - angleRange.x) * 32u));
				uint maskedBits = ((1 << bitsRange.y) - 1) << bitsRange.x;

#ifdef GI
				uint validBits = maskedBits & ~bitmaskGI;

				if (validBits) {
					float3 normalSample = GBuffer::DecodeNormal(srcNormal.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevelRadiance));
					if (dot(samplePos, normalSample) > 0)
						normalSample = -normalSample;
					float frontBackMult = -dot(normalSample, sampleHorizonVec);
					frontBackMult = frontBackMult < 0 ? 0.0 : frontBackMult;

					if (frontBackMult > 0.f) {
						float3 sampleRadiance = srcRadiance.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevelRadiance).rgb * frontBackMult;
						sampleRadiance = max(sampleRadiance, 0);

						float3 diffuseRadiance = sampleRadiance * 4.0 * Math::PI * countbits(validBits) * 0.03125;
						totalRadiance += diffuseRadiance;
#ifdef SSGI_SH
						totalDirection += sampleHorizonVec * Color::RGBToLuminance(diffuseRadiance);
#endif
					}
				}

				bitmaskGI |= maskedBits;
#endif  // GI

				bitmask |= maskedBits;
			}
		}

		visibility += countbits(bitmask) * 0.03125;

#if defined(DYNAMIC_CUBEMAPS)
		if (SpecUseDynamicCubemap != 0) {
			float3 worldPos = ViewToWorldPosition(pixCenterPos, FrameBuffer::CameraViewInverse[eyeIndex]);
			float3 worldNormal = ViewToWorldVector(viewspaceNormal, FrameBuffer::CameraViewInverse[eyeIndex]);

#	if defined(SKYLIGHTING)
			float fadeOutFactor = 1.0;
			float3 skylightingNormal = worldNormal;
			float skylightingBoost = 1.0;
			if (!SharedData::InInterior) {
				fadeOutFactor = Skylighting::GetFadeOutFactor(worldPos);
				skylightingNormal = normalize(float3(worldNormal.xy, max(0, worldNormal.z)));
				skylightingBoost = 1.0 + saturate(worldNormal.z) * (1.0 - SharedData::skylightingSettings.MinDiffuseVisibility);
			}
#	endif

			const uint FALLBACK_SAMPLES = 4;
			const uint BITS_PER_SAMPLE = 32 / FALLBACK_SAMPLES;
			uint bitmaskCopy = bitmask;
			float orthoLen = max(dot(orthoDirectionVec, orthoDirectionVec), EPSILON_LENGTH_SQ);
			float3 sliceTangent = orthoDirectionVec * rsqrt(orthoLen);

			for (uint j = 0; j < FALLBACK_SAMPLES; j++) {
				uint segMask = (1u << BITS_PER_SAMPLE) - 1u;
				uint occluded = countbits(bitmaskCopy & segMask);
				float weight = float(BITS_PER_SAMPLE - occluded) * 0.03125;

				if (weight > 0) {
					float cosAngle = (1.0 - ((float(j) + 0.5) / float(FALLBACK_SAMPLES))) * 2.0 - 1.0;
					float angle = FastMath::ACos(cosAngle);
					float3 rayDir = normalize(sliceTangent * cos(angle) + viewspaceNormal * sin(angle));
					rayDir = normalize(rayDir - axisVec * dot(rayDir, axisVec));

					float3 worldDir = ViewToWorldVector(rayDir, FrameBuffer::CameraViewInverse[eyeIndex]);
					float3 envColor = EnvReflectionsTexture.SampleLevel(samplerLinearClamp, worldDir, 0);

#	if defined(SKYLIGHTING)
					if (!SharedData::InInterior) {
						sh2 skylightingSH = Skylighting::Sample(worldPos, worldDir);
						float skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, skylightingNormal, fadeOutFactor);
						skylightingDiffuse *= skylightingBoost;

						float3 envNoSkyColor = EnvTexture.SampleLevel(samplerLinearClamp, worldDir, 0);
						float3 skyColor = max(envColor - envNoSkyColor, 0);
						envColor = envNoSkyColor * skylightingDiffuse + skyColor * skylightingDiffuse;
					}
#	endif

					envColor = Color::IrradianceToLinear(envColor);
					totalRadiance += envColor * weight;
				}

				bitmaskCopy >>= BITS_PER_SAMPLE;
			}
		}
#endif
	}

	visibility = saturate(visibility);
	visibility = 1 - pow(abs(1 - visibility), AOPower);

	o_ao = visibility;
	o_radiance = totalRadiance;
#ifdef SSGI_SH
	o_direction = normalize(totalDirection + 1e-6);
#endif
}

[numthreads(8, 8, 1)] void main(const uint2 dtid : SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

#if defined(SSGI_HALF)
	uint colOffset = (dtid.y + FrameIndex) & 1;
	uint2 pxCoord = uint2(dtid.x * 2 + colOffset, dtid.y);
	uint2 outCoord = dtid.xy;
#else
	uint2 pxCoord = dtid;
	uint2 outCoord = dtid;
#endif

	float2 uv = (pxCoord + .5) * RCP_OUT_FRAME_DIM;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);

	float2 normalSample = FULLRES_LOAD(srcNormal, pxCoord, uv * OUT_FRAME_SCALE, samplerLinearClamp);
	float3 viewspaceNormal = GBuffer::DecodeNormal(normalSample);

	float3 worldNormal = ViewToWorldVector(viewspaceNormal, FrameBuffer::CameraViewInverse[eyeIndex]);
	half2 encodedWorldNormal = GBuffer::EncodeNormal(worldNormal);
	outPrevGeo[pxCoord] = half3(viewspaceZ, encodedWorldNormal);

	float ao = 0;
	float3 radiance = 0;
#ifdef SSGI_SH
	float3 direction = 0;
#endif

	bool needGI = viewspaceZ > FP_Z;
	if (needGI) {
		CalculateGI(pxCoord, uv, viewspaceZ, viewspaceNormal, ao, radiance
#ifdef SSGI_SH
			,
			direction
#endif
		);
	}

	radiance = filterNaN(radiance);

#ifdef SSGI_SH
	float4 sh1;
	outRadianceHitDist[outCoord] = REBLUR_FrontEnd_PackSh(radiance, ao, direction, sh1, true);
	outSH1[outCoord] = sh1;
#else
	outRadianceHitDist[outCoord] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(radiance, ao, true);
#endif
}