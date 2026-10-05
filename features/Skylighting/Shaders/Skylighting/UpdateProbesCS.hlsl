#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Skylighting/Skylighting.hlsli"

Texture2D<unorm float> srcOcclusionDepth : register(t0);
Texture2DArray<float4> ShadowCascadeMap : register(t1);
Texture2DArray<float4> ESRAMShadow : register(t3);

struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};
StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t2);

RWTexture3D<sh2> outProbeArray : register(u0);
RWTexture3D<uint> outAccumFramesArray : register(u1);
RWTexture3D<uint> outShadowBitmask : register(u2);
RWTexture3D<float> outShadowVisibility : register(u3);

SamplerComparisonState comparisonSampler : register(s0);

static const float3 noise3D[32] = {
	float3(0.247, -0.583, 0.891),
	float3(-0.672, 0.315, -0.428),
	float3(0.934, 0.762, -0.153),
	float3(-0.391, -0.847, 0.526),
	float3(0.618, 0.094, 0.739),
	float3(-0.825, -0.271, -0.683),
	float3(0.152, 0.968, 0.347),
	float3(0.503, -0.714, -0.592),
	float3(-0.436, 0.629, 0.814),
	float3(0.887, -0.198, 0.461),
	float3(-0.759, 0.852, -0.305),
	float3(0.321, -0.476, -0.921),
	float3(-0.094, 0.543, -0.768),
	float3(0.776, 0.418, 0.632),
	float3(-0.538, -0.695, 0.279),
	float3(0.649, -0.921, 0.186),
	float3(-0.913, 0.127, 0.574),
	float3(0.285, 0.806, -0.447),
	float3(0.471, -0.352, 0.698),
	float3(-0.627, -0.194, -0.856),
	float3(0.834, 0.591, -0.712),
	float3(-0.173, -0.968, -0.421),
	float3(0.562, 0.239, -0.785),
	float3(-0.745, 0.487, 0.316),
	float3(0.108, -0.631, 0.894),
	float3(0.926, -0.845, -0.267),
	float3(-0.384, 0.712, -0.539),
	float3(0.697, 0.163, 0.825),
	float3(-0.851, -0.429, 0.641),
	float3(0.214, 0.934, 0.372),
	float3(0.578, -0.762, -0.614),
	float3(-0.469, 0.381, 0.947)
};

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID) {
	const float fadeInThreshold = 15;
	const static sh2 unitSH = Skylighting::UNIT_SH;
	const SharedData::SkylightingSettings settings = SharedData::skylightingSettings;
	uint3 cellID = ((uint3)dtid - settings.ArrayOrigin.xyz) % Skylighting::ARRAY_DIM;
	uint3 validMin = (uint3)max(0, settings.ValidMargin.xyz);
	uint3 validMax = Skylighting::ARRAY_DIM - 1 + (uint3)min(0, settings.ValidMargin.xyz);
	bool isValid = all(cellID >= validMin) && all(cellID <= validMax);  // check if the cell is newly added
	float3 cellCentreMS = cellID + 0.5 - Skylighting::ARRAY_DIM / 2;
	cellCentreMS = cellCentreMS / Skylighting::ARRAY_DIM * Skylighting::ARRAY_SIZE + settings.PosOffset.xyz;

	float3 cellCentreOS = mul(settings.OcclusionViewProj, float4(cellCentreMS, 1)).xyz;
	cellCentreOS.y = -cellCentreOS.y;
	float2 occlusionUV = cellCentreOS.xy * 0.5 + 0.5;

	if (all(occlusionUV > 0) && all(occlusionUV < 1)) {
		uint accumFrames = isValid ? (outAccumFramesArray[dtid] + 1) : 1;
		float visibility = srcOcclusionDepth.SampleCmpLevelZero(comparisonSampler, occlusionUV, cellCentreOS.z);

		sh2 occlusionSH = settings.OcclusionSHBasis4Pi * visibility;
		if (isValid) {
			float lerpFactor = rcp(accumFrames);
			sh2 prevProbeSH = unitSH;
			if (accumFrames > 1)
				prevProbeSH += (outProbeArray[dtid] - unitSH) * fadeInThreshold / min(fadeInThreshold, accumFrames - 1);  // inverse confidence
			occlusionSH = lerp(prevProbeSH, occlusionSH, lerpFactor);
		}
		occlusionSH = lerp(unitSH, occlusionSH, min(fadeInThreshold, accumFrames) / fadeInThreshold);  // confidence fade in

		outProbeArray[dtid] = occlusionSH;
		outAccumFramesArray[dtid] = accumFrames;
	} else if (!isValid) {
		outProbeArray[dtid] = unitSH;
		outAccumFramesArray[dtid] = 0;
	}

	// Shadow cascade sampling with bitmask accumulation
	float4 cellCentreCS = mul(FrameBuffer::CameraViewProj, float4(cellCentreMS, 1));
	bool onScreen = false;
	if (cellCentreCS.w > 0) {
		float2 screenUV = (cellCentreCS.xy / cellCentreCS.w) * float2(0.5, -0.5) + 0.5;
		onScreen = all(screenUV > 0) && all(screenUV < 1);
	}

	// Only a valid projection advances the history. Clamping an out-of-range one would sample the
	// shadow map border into the history, and the jitter can cross the camera near plane.
	// The history is a shift register of the last 32 accepted samples, so a rejected sample leaves no stale bit.
	uint jitterIndex = SharedData::FrameCountAlwaysActive % 32;
	bool advanceShadowHistory = false;
	float shadowSample = 1.0;
	if (onScreen) {
		DirectionalShadowLightData shadowData = DirectionalShadowLights[0];

		float3 jitteredMS = cellCentreMS + noise3D[jitterIndex] * 128;
		float4 jitteredCS = mul(FrameBuffer::CameraViewProj, float4(jitteredMS, 1));

		if (jitteredCS.w > 0) {
			float ndcDepth = jitteredCS.z / jitteredCS.w;
			float linearDepth = SharedData::GetScreenDepth(ndcDepth);

			if (ndcDepth > 0 && ndcDepth < 1 && linearDepth > 0) {
				if (linearDepth >= shadowData.EndSplitDistances.y) {
					// Past the directional shadow range: fully lit
					advanceShadowHistory = true;
				} else {
					float3 positionWS = jitteredMS + FrameBuffer::CameraPosAdjust.xyz;

					uint cascadeIndex = (linearDepth > shadowData.EndSplitDistances.x) ? 1u : 0u;
					float3 positionLS = mul(shadowData.ShadowProj[cascadeIndex], float4(positionWS, 1)).xyz;
					bool inCascade = all(positionLS.xy > 0) && all(positionLS.xy < 1) && positionLS.z > 0 && positionLS.z < 1;
					// The jitter can leave cascade 0's footprint; cascade 1 covers a wider area, so try it next.
					if (!inCascade && cascadeIndex == 0) {
						cascadeIndex = 1;
						positionLS = mul(shadowData.ShadowProj[1], float4(positionWS, 1)).xyz;
						inCascade = all(positionLS.xy > 0) && all(positionLS.xy < 1) && positionLS.z > 0 && positionLS.z < 1;
					}

					if (inCascade) {
						float cascadeShadow = ShadowCascadeMap.SampleCmpLevelZero(comparisonSampler, float3(positionLS.xy, cascadeIndex), positionLS.z);
						float esramShadow = ESRAMShadow.SampleCmpLevelZero(comparisonSampler, float3(positionLS.xy, cascadeIndex), positionLS.z);
						shadowSample = min(cascadeShadow, esramShadow);

						float fade = saturate(linearDepth / shadowData.EndSplitDistances.y);
						float fadeFactor = 1.0 - pow(fade * fade, 8);
						shadowSample = lerp(1.0, shadowSample, fadeFactor);
						advanceShadowHistory = true;
					}
				}
			}
		}
	}

	if (advanceShadowHistory) {
		// Unseeded history starts fully lit to match the cleared visibility of 1.0
		uint bitmask = isValid ? outShadowBitmask[dtid] : 0xFFFFFFFFu;
		bitmask = (bitmask << 1) | (shadowSample > 0.5 ? 1u : 0u);

		outShadowBitmask[dtid] = bitmask;

		float shadow = float(countbits(bitmask)) / 32.0;
		outShadowVisibility[dtid] = shadow;
	} else if (!isValid) {
		outShadowBitmask[dtid] = 0xFFFFFFFFu;
		outShadowVisibility[dtid] = 1.0;
	}
}
