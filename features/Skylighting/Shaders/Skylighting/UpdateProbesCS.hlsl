#include "Common/Math.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"

#include "Skylighting/Skylighting.hlsli"

struct ShadowData
{
	float4 VPOSOffset;
	float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam;
	float4 DebugColor;
	float4 PropertyColor;
	float4 AlphaTestRef;
	float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4];
	// Since ShadowData is passed between c++ and hlsl, can't have different defines due to strong typing
	float4x3 ShadowMapProj[2][3];
	float4x4 CameraViewProjInverse[2];
};

Texture2D<unorm float> srcOcclusionDepth : register(t0);
Texture2DArray<float> SharedShadowMap : register(t1);      // Shadow cascade texture
StructuredBuffer<ShadowData> SharedShadowData : register(t2);  // Shadow data buffer

RWTexture3D<sh2> outProbeArray : register(u0);
RWTexture3D<uint> outAccumFramesArray : register(u1);
RWTexture3D<uint> outShadowVisibilityBitArray : register(u2);
RWTexture3D<uint> outShadowVisibilityBitShiftArray : register(u3);
RWTexture3D<float> outShadowVisibilityArray : register(u4);

SamplerComparisonState comparisonSampler : register(s0);

float GetShadowDepth(float3 positionWS, uint eyeIndex)
{
	float4 positionCSShifted = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
	return positionCSShifted.z / positionCSShifted.w;
}

float Get2DFilteredShadow(float3 positionWS, uint eyeIndex, out bool validShadow)
{
	validShadow = true;

	ShadowData sD = SharedShadowData[0];

	float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

	if (sD.EndSplitDistances.z >= shadowMapDepth) {
		float4x3 lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][0];
		float shadowMapThreshold = sD.AlphaTestRef.y;
		float cascadeIndex = 0;
		if (2.5 < sD.EndSplitDistances.w && sD.EndSplitDistances.y < shadowMapDepth) {
			lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][2];
			shadowMapThreshold = sD.AlphaTestRef.z;
			cascadeIndex = 2;
		} else if (sD.EndSplitDistances.x < shadowMapDepth) {
			lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][1];
			shadowMapThreshold = sD.AlphaTestRef.z;
			cascadeIndex = 1;
		}

		float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;

		if (saturate(positionLS.x) != positionLS.x || saturate(positionLS.y) != positionLS.y)
		{
			validShadow = false;
			return 0.0;
		}

		float shadowVisibility = SharedShadowMap.SampleCmpLevelZero(comparisonSampler, float3(positionLS.xy, cascadeIndex), positionLS.z - shadowMapThreshold);

		if (cascadeIndex < 1 && sD.StartSplitDistances.y < shadowMapDepth) {
			float3 cascade1PositionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS.xyz, 1)).xyz;

			if (saturate(cascade1PositionLS.x) != cascade1PositionLS.x || saturate(cascade1PositionLS.y) != cascade1PositionLS.y)
			{
				validShadow = false;
				return 0.0;
			}

			float cascade1ShadowVisibility = SharedShadowMap.SampleCmpLevelZero(comparisonSampler, float3(cascade1PositionLS.xy, 1), cascade1PositionLS.z - shadowMapThreshold);

			float cascade1BlendFactor = smoothstep(0, 1, (shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));
			shadowVisibility = lerp(shadowVisibility, cascade1ShadowVisibility, cascade1BlendFactor);
		}

		return shadowVisibility;
	}

	return 1.0;
}


[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID) {
	const float fadeInThreshold = 15;
	const static sh2 unitSH = float4(sqrt(4.0 * Math::PI), 0, 0, 0);
	const SharedData::SkylightingSettings settings = SharedData::skylightingSettings;
	uint3 cellID = uint3(max(int3(dtid) - settings.ArrayOrigin.xyz, 0) % Skylighting::ARRAY_DIM);
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
		float occlusionDepth = srcOcclusionDepth.SampleCmpLevelZero(comparisonSampler, occlusionUV, 0);
		float visibility = srcOcclusionDepth.SampleCmpLevelZero(comparisonSampler, occlusionUV, cellCentreOS.z);

		sh2 occlusionSH = SphericalHarmonics::Scale(SphericalHarmonics::Evaluate(settings.OcclusionDir.xyz), visibility * 4.0 * Math::PI);  // 4 pi from monte carlo
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

	uint shadowVisibilityBitShift = outShadowVisibilityBitShiftArray[dtid];

	cellCentreMS += noise3D[shadowVisibilityBitShift] * Skylighting::CELL_SIZE * 0.5;

	float3 viewDirection = FrameBuffer::WorldToView(-normalize(cellCentreMS), false);
	float2 uv = FrameBuffer::ViewToUV(viewDirection, false);
	
	if (!FrameBuffer::IsOutsideFrame(uv) && viewDirection.z < 0.0) {  // Check that the view direction exists in screenspace and that it is in front of the camera
		bool validShadow;
		uint hasShadowVisibility = Get2DFilteredShadow(cellCentreMS, 0, validShadow) > 0.5;

		if (validShadow){
			uint shadowVisibilityBits = isValid ? outShadowVisibilityBitArray[dtid] : 0;

			shadowVisibilityBits &= ~(1u << shadowVisibilityBitShift);
			shadowVisibilityBits |= (hasShadowVisibility << shadowVisibilityBitShift);

			shadowVisibilityBitShift = (shadowVisibilityBitShift + 1) % 32;

			float shadowVisibility = float(countbits(shadowVisibilityBits)) / 32.0;
			
			ShadowData sD = SharedShadowData[0];
			float fadeFactor = 1.0 - pow(saturate(dot(cellCentreMS.xyz, cellCentreMS.xyz) / sD.ShadowLightParam.z), 8);

			float skylightingFadeFactor = Skylighting::getFadeOutFactor(cellCentreMS);
			shadowVisibility = lerp(1.0, shadowVisibility, min(fadeFactor, skylightingFadeFactor));

			outShadowVisibilityBitArray[dtid] = shadowVisibilityBits;
			outShadowVisibilityBitShiftArray[dtid] = shadowVisibilityBitShift;
			outShadowVisibilityArray[dtid] = shadowVisibility;
		}
	}
}