#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/VR.hlsli"

#if defined(CSHADER)
SamplerState ShadowmapSampler : register(s0);
SamplerState ShadowmapVLSampler : register(s1);
SamplerState InverseRepartitionSampler : register(s2);
SamplerState NoiseSampler : register(s3);

Texture2DArray<float4> ShadowmapTex : register(t0);
Texture2DArray<float4> ShadowmapVLTex : register(t1);
Texture1D<float4> InverseRepartitionTex : register(t2);
Texture3D<float4> NoiseTex : register(t3);

RWTexture3D<float4> DensityRW : register(u0);
RWTexture3D<float4> DensityCopyRW : register(u1);

#	define LinearSampler ShadowmapSampler

#	include "Common/Framebuffer.hlsli"
#	include "Common/SharedData.hlsli"

#	if defined(TERRAIN_SHADOWS)
#		include "TerrainShadows/TerrainShadows.hlsli"
#	endif

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

#	include "Common/ShadowSampling.hlsli"

cbuffer PerTechnique : register(b0)
{
#	ifndef VR
	float4x4 CameraViewProj[1] : packoffset(c0);
	float4x4 CameraViewProjInverse[1] : packoffset(c4);
	float4x3 ShadowMapProj[1][3] : packoffset(c8);
	float3 EndSplitDistances : packoffset(c17.x);
	float ShadowMapCount : packoffset(c17.w);
	float EnableShadowCasting : packoffset(c18);
	float3 DirLightDirection : packoffset(c19);
	float3 TextureDimensions : packoffset(c20);
	float3 WindInput[1] : packoffset(c21);
	float InverseDensityScale : packoffset(c21.w);
	float3 PosAdjust[1] : packoffset(c22);
	float IterationIndex : packoffset(c22.w);
	float PhaseContribution : packoffset(c23.x);
	float PhaseScattering : packoffset(c23.y);
	float DensityContribution : packoffset(c23.z);
#	else
	float4x4 CameraViewProj[2] : packoffset(c0);
	float4x4 CameraViewProjInverse[2] : packoffset(c8);
	float4x3 ShadowMapProj[2][3] : packoffset(c16);
	float3 EndSplitDistances : packoffset(c34.x);
	float ShadowMapCount : packoffset(c34.w);
	float EnableShadowCasting : packoffset(c35.x);
	float3 DirLightDirection : packoffset(c36);
	float3 TextureDimensions : packoffset(c37);
	float3 WindInput[2] : packoffset(c38);
	float InverseDensityScale : packoffset(c39.w);
	float3 PosAdjust[2] : packoffset(c40);
	float IterationIndex : packoffset(c41.w);
	float PhaseContribution : packoffset(c42.x);
	float PhaseScattering : packoffset(c42.y);
	float DensityContribution : packoffset(c42.z);
#	endif
}

[numthreads(32, 32, 1)] void main(uint3 dispatchID
								  : SV_DispatchThreadID) {
	const float3 StepCoefficients[] = { { 0, 0, 0 },
		{ 0, 0, 1.000000 },
		{ 0, 1.000000, 0 },
		{ 0, 1.000000, 1.000000 },
		{ 1.000000, 0, 0 },
		{ 1.000000, 0, 1.000000 },
		{ 1.000000, 1.000000, 0 },
		{ 1.000000, 1.000000, 1.000000 } };

	float3 normalizedCoordinates = dispatchID.xyz / TextureDimensions.xyz;
	float2 uv = normalizedCoordinates.xy;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	float3 depthUv = Stereo::ConvertFromStereoUV(normalizedCoordinates, eyeIndex) + 0.001 * StepCoefficients[IterationIndex].xyz;
	float depth = InverseRepartitionTex.SampleLevel(InverseRepartitionSampler, depthUv.z, 0).x;
	float4 positionCS = float4(2 * depthUv.x - 1, 1 - 2 * depthUv.y, depth, 1);

	float4 positionWS = mul(transpose(CameraViewProjInverse[eyeIndex]), positionCS);
	positionWS /= positionWS.w;

	float4 positionCSShifted = mul(transpose(CameraViewProj[eyeIndex]), positionWS);
	positionCSShifted /= positionCSShifted.w;

	float shadowMapDepth = positionCSShifted.z;

	float shadowContribution = 1;
	if (EndSplitDistances.z >= shadowMapDepth) {
		float4x3 lightProjectionMatrix = ShadowMapProj[eyeIndex][0];
		float shadowMapThreshold = 0.01;
		float cascadeIndex = 0;
		if (2.5 < ShadowMapCount && EndSplitDistances.y < shadowMapDepth) {
			lightProjectionMatrix = ShadowMapProj[eyeIndex][2];
			shadowMapThreshold = 0;
			cascadeIndex = 2;
		} else if (EndSplitDistances.x < shadowMapDepth) {
			lightProjectionMatrix = ShadowMapProj[eyeIndex][1];
			shadowMapThreshold = 0;
			cascadeIndex = 1;
		}

		float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;
		float shadowMapValue = ShadowmapTex.SampleLevel(ShadowmapSampler, float3(positionLS.xy, cascadeIndex), 0).x;

		if (EnableShadowCasting < 0.5) {
			float shadowMapVLValue = ShadowmapVLTex.SampleLevel(ShadowmapVLSampler, float3(positionLS.xy, cascadeIndex), 0).x;

			float baseShadowVisibility = 0;
			if (shadowMapValue >= positionLS.z - shadowMapThreshold) {
				baseShadowVisibility = 1;
			}

			float vlShadowVisibility = 0;
			if (shadowMapVLValue >= positionLS.z - shadowMapThreshold) {
				vlShadowVisibility = 1;
			}

			shadowContribution = min(baseShadowVisibility, vlShadowVisibility);
		} else if (shadowMapValue >= positionLS.z - shadowMapThreshold) {
			shadowContribution = 1;
		} else {
			shadowContribution = 0;
		}
	}

	float3 noiseUv = 0.0125 * (InverseDensityScale * (positionWS.xyz + WindInput[eyeIndex]));
	float noise = NoiseTex.SampleLevel(NoiseSampler, noiseUv, 0).x;
	float densityFactor = noise * (1 - 0.75 * smoothstep(0, 1, saturate(2 * positionWS.z / 300)));
	float densityContribution = lerp(1, densityFactor, DensityContribution);

	float LdotN = dot(normalize(-positionWS.xyz), normalize(DirLightDirection));
	float phaseFactor = (1 - PhaseScattering * PhaseScattering) / (4 * Math::PI * (1 - LdotN * PhaseScattering));
	float phaseContribution = lerp(1, phaseFactor, PhaseContribution);

	if (shadowContribution != 0.0 && !SharedData::InInterior && !SharedData::HideSky)
		shadowContribution *= ShadowSampling::GetWorldShadow(positionWS.xyz, PosAdjust[eyeIndex].xyz, eyeIndex);

	float vl = shadowContribution * densityContribution * phaseContribution;

	DensityRW[dispatchID.xyz] = vl;
	DensityCopyRW[dispatchID.xyz] = vl;
}
#endif
