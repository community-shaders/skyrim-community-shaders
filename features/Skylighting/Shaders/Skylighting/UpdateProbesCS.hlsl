#include "Common/Math.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

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
Texture2DArray<float4> SharedShadowMap : register(t1);      // Shadow cascade texture
StructuredBuffer<ShadowData> SharedShadowData : register(t2);  // Shadow data buffer

RWTexture3D<sh2> outProbeArray : register(u0);
RWTexture3D<uint> outAccumFramesArray : register(u1);
RWTexture3D<float> outShadowVisibilityArray : register(u2);     // Shadow visibility storage
RWTexture3D<uint> outShadowVisibilityAccumFrames : register(u3);  // Shadow visibility accumulation

SamplerComparisonState comparisonSampler : register(s0);

float GetShadowDepth(float3 positionWS, uint eyeIndex)
{
	float4 positionCSShifted = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
	return positionCSShifted.z / positionCSShifted.w;
}

float Get2DFilteredShadowCascade(float2x2 rotationMatrix, float sampleOffsetScale, float2 baseUV, float cascadeIndex, float compareValue, uint eyeIndex)
{
	float layerIndexRcp = rcp(1 + cascadeIndex);

	float visibility = 0.0;

	compareValue -= 0.0001;

	float2 sampleOffset = mul(float2(-1.0, 1.0), rotationMatrix);
	float2 sampleUV = layerIndexRcp * sampleOffset * sampleOffsetScale + baseUV;
	visibility += SharedShadowMap.SampleCmpLevelZero(comparisonSampler, float3(sampleUV, cascadeIndex), compareValue).x;

	return visibility;
}

float Get2DFilteredShadow(float2x2 rotationMatrix, float3 positionWS, uint eyeIndex)
{
	ShadowData sD = SharedShadowData[0];

	float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

	if (sD.EndSplitDistances.z >= shadowMapDepth) {
		float4x3 lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][0];
		float cascadeIndex = 0;

		if (sD.EndSplitDistances.x < shadowMapDepth) {
			lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][1];
			cascadeIndex = 1;
		}

		sD.ShadowSampleParam.z *= 10.0;

		float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;

		float shadowVisibility = Get2DFilteredShadowCascade(rotationMatrix, sD.ShadowSampleParam.z, positionLS.xy, cascadeIndex, positionLS.z, eyeIndex);

		if (cascadeIndex < 1 && sD.StartSplitDistances.y < shadowMapDepth) {
			float3 cascade1PositionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS.xyz, 1)).xyz;

			float cascade1ShadowVisibility = Get2DFilteredShadowCascade(rotationMatrix, sD.ShadowSampleParam.z, cascade1PositionLS.xy, 1, cascade1PositionLS.z, eyeIndex);

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

	uint accumFrames = isValid ? (outShadowVisibilityAccumFrames[dtid] + 1) : 1;

	// Shadow cascade visibility sampling
	float noise = Random::InterleavedGradientNoise(0, SharedData::FrameCount);
	float2 rotation;
	sincos(Math::TAU * noise, rotation.y, rotation.x);
	float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

	float3 viewDirection = FrameBuffer::WorldToView(-normalize(cellCentreMS), false);
	float2 uv = FrameBuffer::ViewToUV(viewDirection, false);

	if (!FrameBuffer::IsOutsideFrame(uv) && viewDirection.z < 0.0) {  // Check that the view direction exists in screenspace and that it is in front of the camera
		float shadowVisibility = Get2DFilteredShadow(rotationMatrix, cellCentreMS, 0);
		if (isValid) {
			float lerpFactor = rcp(accumFrames);
			float prevProbeShadow = 1.0;
			if (accumFrames > 1)
				prevProbeShadow += (outShadowVisibilityArray[dtid] - 1.0) * fadeInThreshold / min(fadeInThreshold, accumFrames - 1);  // inverse confidence
			shadowVisibility = lerp(prevProbeShadow, shadowVisibility, lerpFactor);
		}
		shadowVisibility = lerp(1.0, shadowVisibility, min(fadeInThreshold, accumFrames) / fadeInThreshold);  // confidence fade in

		outShadowVisibilityArray[dtid] = shadowVisibility;
		outShadowVisibilityAccumFrames[dtid] = min(60, accumFrames);
	} else if (!isValid){
		outShadowVisibilityArray[dtid] = 1.0;
		outShadowVisibilityAccumFrames[dtid] = 0;
	}
}