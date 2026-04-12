// SSRT3 - Screen Space Ray Tracing 3
// Ported from SSRT3 by CDRIN (Olivier Therrien)
// https://github.com/cdrinmatern/SSRT3
//
// Based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion"
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
// Horizon-based sampling with 32-bit bitfield visibility tracking

#include "Common/FastMath.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/VR.hlsli"
#include "SSRT/common.hlsli"

#if defined(FALLBACK)
#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
Texture3D<sh2> SkylightingProbeArray : register(t3);
Texture2DArray<float3> stbn_vec3_2Dx1D_128x128x64 : register(t4);
#	endif

#	if defined(IBL)
#		include "Common/Color.hlsli"
#		include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
Texture2D<sh2> EnvIBLTexture : register(t5);
Texture2D<sh2> SkyIBLTexture : register(t6);
#	endif
#endif

Texture2D<float> srcDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float3> srcRadiance : register(t2);

RWTexture2D<float4> outGIOcclusion : register(u0);

#define TILE_SIZE 8

// SSRT3: ComputeOccludedBitfield
uint ComputeOccludedBitfield(float minHorizon, float maxHorizon, inout uint globalOccludedBitfield, out uint numOccludedZones)
{
	uint startHorizonInt = minHorizon * MAX_RAY;
	uint angleHorizonInt = ceil(saturate(maxHorizon - minHorizon) * MAX_RAY);
	uint angleHorizonBitfield = angleHorizonInt > 0 ? (0xFFFFFFFF >> ((32 - MAX_RAY) + (MAX_RAY - angleHorizonInt))) : 0;
	uint currentOccludedBitfield = angleHorizonBitfield << startHorizonInt;
	currentOccludedBitfield = currentOccludedBitfield & (~globalOccludedBitfield);
	globalOccludedBitfield = globalOccludedBitfield | currentOccludedBitfield;
	numOccludedZones = countbits(currentOccludedBitfield);
	return currentOccludedBitfield;
}

// SSRT3: HorizonSampling
float3 HorizonSampling(
	bool directionIsRight, float3 posVS, float2 slideDir_TexelSize, float initialRayStep,
	float2 uv, float3 viewDir, float3 normalVS, float n, inout uint globalOccludedBitfield,
	float3 planeNormal, uint eyeIndex)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	float stepRadius = ScreenSpaceSampling ?
	                       (Radius * (FrameDim.x / 2)) / (float)StepCount :
	                       max((Radius * HalfProjScale) / posVS.z, (float)StepCount);
	stepRadius /= ((float)StepCount + 1);
	float radiusVS = max(1, float(StepCount - 1)) * stepRadius;
	float samplingDirection = directionIsRight ? 1 : -1;
	float3 col = 0;
	float3 lastSamplePosVS = posVS;

	[loop] for (uint j = 0; j < StepCount; j++)
	{
		float offset = pow(abs((stepRadius * (j + initialRayStep)) / radiusVS), ExpFactor) * radiusVS;
		float2 uvOffset = slideDir_TexelSize * max(offset, 1 + j);
		float2 sampleUV = uv + uvOffset * samplingDirection;

		uint sampleEyeIndex = eyeIndex;
#ifdef VR
		sampleEyeIndex = Stereo::GetEyeIndexFromTexCoord(sampleUV);
#endif
		float2 sampleScreenPos = Stereo::ConvertFromStereoUV(sampleUV, sampleEyeIndex);
		[branch] if (sampleScreenPos.x <= 0 || sampleScreenPos.y <= 0 || sampleScreenPos.x >= 1 || sampleScreenPos.y >= 1)
			break;

		float SZ = ScreenToViewDepth(srcDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0));
		float3 samplePosVS = ScreenToViewPosition(sampleScreenPos, SZ, sampleEyeIndex);

#ifdef VR
		if (sampleEyeIndex != eyeIndex) {
			if (abs(SZ - posVS.z) > posVS.z * 0.1)
				continue;
			samplePosVS = FrameBuffer::WorldToView(FrameBuffer::ViewToWorld(samplePosVS, true, sampleEyeIndex), true, eyeIndex);
		}
#endif

		float3 pixelToSample = normalize(samplePosVS - posVS);
		float linearThicknessMultiplier = LinearThickness ? saturate(samplePosVS.z / 100000.0) * 100 : 1;
		float3 pixelToSampleBackface = normalize((samplePosVS - (linearThicknessMultiplier * viewDir * Thickness)) - posVS);

		float2 frontBackHorizon = float2(dot(pixelToSample, viewDir), dot(pixelToSampleBackface, viewDir));
		frontBackHorizon = GTAOFastAcos(clamp(frontBackHorizon, -1, 1));
		frontBackHorizon = saturate(((samplingDirection * -frontBackHorizon) - n + Math::HALF_PI) / Math::PI);
		frontBackHorizon = directionIsRight ? frontBackHorizon.yx : frontBackHorizon.xy;

		uint numOccludedZones;
		ComputeOccludedBitfield(frontBackHorizon.x, frontBackHorizon.y, globalOccludedBitfield, numOccludedZones);

		if (numOccludedZones > 0) {
			float3 lightColor = srcRadiance.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0).rgb;
			float luminance = dot(lightColor, float3(0.2126, 0.7152, 0.0722));

			if (luminance > 0.001) {
				float3 lightDirectionVS = normalize(pixelToSample);
				float normalDotLightDirection = saturate(dot(normalVS, lightDirectionVS));

				if (normalDotLightDirection > 0.001) {
					float3 lightNormalVS;

#	ifdef NORMAL_APPROXIMATION
					lightNormalVS = -samplingDirection * cross(normalize(samplePosVS - lastSamplePosVS), planeNormal);
#	else
					lightNormalVS = GBuffer::DecodeNormal(srcNormalRoughness.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0).xy);
#	endif

					float lightNormalDotLightDirection = dot(lightNormalVS, -lightDirectionVS);
					if (BackfaceLighting > 0 && dot(lightNormalVS, viewDir) > 0) {
						lightNormalDotLightDirection = sign(lightNormalDotLightDirection) < 0 ?
						                                  abs(lightNormalDotLightDirection) * BackfaceLighting :
						                                  abs(lightNormalDotLightDirection);
					} else {
						lightNormalDotLightDirection = saturate(lightNormalDotLightDirection);
					}

					col.xyz += (float(numOccludedZones) / float(MAX_RAY)) * lightColor * normalDotLightDirection * lightNormalDotLightDirection;
				}
			}
		}

		lastSamplePosVS = samplePosVS;
	}

	return col;
}

#if defined(FALLBACK)
// Sample ambient/indirect light for a given world-space direction
// Replaces SSRT3's TraceReflectionProbes + sky reflection with CS's DALC/IBL/Skylighting
float3 SampleFallbackAmbient(float3 rayDirWS, float3 positionWS, float3 normalWS, float2 screenPosition, uint eyeIndex)
{
	float3 ambientColor = 0;

#	if defined(IBL)
	// Sample IBL spherical harmonics for this direction
	{
		sh2 shR = EnvIBLTexture.Load(int3(0, 0, 0));
		sh2 shG = EnvIBLTexture.Load(int3(1, 0, 0));
		sh2 shB = EnvIBLTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDirWS);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDirWS);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDirWS);
		float3 envIBL = max(0, float3(colorR, colorG, colorB) / Math::PI);

		sh2 skyR = SkyIBLTexture.Load(int3(0, 0, 0));
		sh2 skyG = SkyIBLTexture.Load(int3(1, 0, 0));
		sh2 skyB = SkyIBLTexture.Load(int3(2, 0, 0));
		float skyColorR = SphericalHarmonics::SHHallucinateZH3Irradiance(skyR, rayDirWS);
		float skyColorG = SphericalHarmonics::SHHallucinateZH3Irradiance(skyG, rayDirWS);
		float skyColorB = SphericalHarmonics::SHHallucinateZH3Irradiance(skyB, rayDirWS);
		float3 skyIBL = max(0, float3(skyColorR, skyColorG, skyColorB) / Math::PI);

		ambientColor = envIBL + skyIBL;
	}
#	else
	// Fallback to DALC (directional ambient lighting coefficients)
	ambientColor = SharedData::GetAmbient(rayDirWS);
#	endif

#	if defined(SKYLIGHTING)
	// Modulate by skylighting visibility
	{
		float3 positionMS = positionWS - FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
		float fadeOut = Skylighting::getFadeOutFactor(positionMS);

		if (fadeOut > 0) {
			sh2 skylightingSH = Skylighting::sample(
				SharedData::skylightingSettings,
				SkylightingProbeArray,
				stbn_vec3_2Dx1D_128x128x64,
				screenPosition,
				positionMS,
				normalWS);

			sh2 rayLobe = SphericalHarmonics::EvaluateCosineLobe(rayDirWS);
			float skylightingVisibility = SphericalHarmonics::FuncProductIntegral(skylightingSH, rayLobe) / Math::PI;
			skylightingVisibility = saturate(skylightingVisibility);
			skylightingVisibility = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylightingVisibility);
			skylightingVisibility = lerp(1.0, skylightingVisibility, fadeOut);

			ambientColor *= skylightingVisibility;
		}
	}
#	endif

	return ambientColor;
}
#endif

[numthreads(TILE_SIZE, TILE_SIZE, 1)] void main(const uint2 dtid : SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + .5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	float2 normalizedScreenPos = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	// Convert raw scene depth to viewspace
	float rawDepth = srcDepth.SampleLevel(samplerPointClamp, uv * frameScale, 0);
	float viewspaceZ = ScreenToViewDepth(rawDepth);

	if (viewspaceZ <= 1e-7) {
		outGIOcclusion[pxCoord] = float4(0, 0, 0, 1);
		return;
	}

	float2 normalSample = srcNormalRoughness.SampleLevel(samplerPointClamp, uv * frameScale, 0).xy;
	float3 viewspaceNormal = GBuffer::DecodeNormal(normalSample);

	float3 posVS = ScreenToViewPosition(normalizedScreenPos, viewspaceZ, eyeIndex);
	float3 viewDir = normalize(-posVS);

	float noiseOffset = SpatialOffsets(pxCoord);
	float noiseDirection = GradientNoise(pxCoord);
	float initialRayStep = frac(noiseOffset + TemporalOffsets) + (ssrtRand(uv) * 2.0 - 1.0) * 1.0 * float(JitterSamples);

	float ao = 0;
	float3 col = 0;
	float3 fallbackColor = 0;
	float2 rcpOutDim = RcpFrameDim;

#if defined(FALLBACK)
	// Precompute world-space data for fallback sampling
	float3 normalWS = ViewToWorldVector(viewspaceNormal, FrameBuffer::CameraViewInverse[eyeIndex]);
	float3 posWS = ViewToWorldPosition(posVS, FrameBuffer::CameraViewInverse[eyeIndex]) + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
#endif

	[loop] for (uint i = 0; i < RotationCount; i++)
	{
		float rotationAngle = (i + noiseDirection + TemporalDirections) * (Math::PI / (float)RotationCount);
		float3 sliceDir = float3(cos(rotationAngle), sin(rotationAngle), 0);
		float2 slideDir_TexelSize = sliceDir.xy * rcpOutDim;
		uint globalOccludedBitfield = 0;

		float3 planeNormal = normalize(cross(sliceDir, viewDir));
		float3 tangent = cross(viewDir, planeNormal);
		float3 projectedNormal = viewspaceNormal - planeNormal * dot(viewspaceNormal, planeNormal);
		float3 projectedNormalNormalized = normalize(projectedNormal);

		float cos_n = clamp(dot(projectedNormalNormalized, viewDir), -1, 1);
		float n = -sign(dot(projectedNormal, tangent)) * acos(cos_n);

		col += HorizonSampling(true, posVS, slideDir_TexelSize, initialRayStep,
			uv, viewDir, viewspaceNormal, n, globalOccludedBitfield, planeNormal, eyeIndex);
		col += HorizonSampling(false, posVS, slideDir_TexelSize, initialRayStep,
			uv, viewDir, viewspaceNormal, n, globalOccludedBitfield, planeNormal, eyeIndex);

		ao += float(countbits(globalOccludedBitfield)) / float(MAX_RAY);

#if defined(FALLBACK)
		// SSRT3: Fallback ambient sampling for unoccluded angular sectors
		if (FallbackSampleCount > 0) {
			float3 realTangent = cross(projectedNormalNormalized, planeNormal);
			uint globalOccludedBitfieldCopy = globalOccludedBitfield;

			[loop] for (uint j = 0; j < FallbackSampleCount; j++)
			{
				uint maskSize = MAX_RAY / FallbackSampleCount;
				uint mask = 0xFFFFFFFF >> (MAX_RAY - maskSize);
				uint hitCount = countbits(globalOccludedBitfieldCopy & mask);

				float cosine = (1.0 - ((float(j) + 0.5) / FallbackSampleCount)) * 2.0 - 1.0;
				float angleCosine = GTAOFastAcos(cosine);
				float3 rayDir = normalize(realTangent * cos(angleCosine) + viewspaceNormal * sin(angleCosine));
				rayDir = normalize(rayDir - planeNormal * dot(rayDir, planeNormal));

				// Convert view-space ray to world-space
				float3 rayDirWS = ViewToWorldVector(rayDir, FrameBuffer::CameraViewInverse[eyeIndex]);

				float3 ambientColor = SampleFallbackAmbient(rayDirWS, posWS, normalWS, pxCoord, eyeIndex);

				fallbackColor += ambientColor * saturate(float(maskSize - hitCount) / float(MAX_RAY));

				globalOccludedBitfieldCopy >>= maskSize;
			}
		}
#endif
	}

	// Finalize AO
	ao /= RotationCount;
	ao = saturate(pow(1.0 - saturate(ao), AOIntensity));

#if defined(FALLBACK)
	// Finalize fallback
	fallbackColor /= RotationCount;
	fallbackColor = saturate(pow(abs(fallbackColor), FallbackPower) * FallbackIntensity);
#endif

	// Finalize GI
	col /= RotationCount;

	// SSRT3: final color = fallback + screen-space GI * intensity
#if defined(FALLBACK)
	col = fallbackColor + col * GIIntensity;
#else
	col = col * GIIntensity;
#endif

	// HSV-space value clamping to prevent fireflies
	col = RgbToHsv(col);
	col.z = clamp(col.z, 0.0, 7);
	col = HsvToRgb(col);

	outGIOcclusion[pxCoord] = float4(col, ao);
}
