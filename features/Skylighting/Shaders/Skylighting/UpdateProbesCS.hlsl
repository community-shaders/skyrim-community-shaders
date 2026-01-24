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

float Get2DFilteredShadow(float3 positionWS, uint index, uint eyeIndex)
{
	ShadowData sD = SharedShadowData[0];

	static const float2 poissonDisk32[32] = {
		float2(-0.613392, 0.617481),
		float2(0.170019, -0.040254),
		float2(-0.299417, 0.791925),
		float2(0.645680, 0.493210),
		float2(-0.651784, 0.717887),
		float2(0.421003, 0.027070),
		float2(-0.817194, -0.271096),
		float2(-0.705374, -0.668203),
		float2(0.977050, -0.108615),
		float2(0.063326, 0.142369),
		float2(0.203528, 0.214331),
		float2(-0.667531, 0.326090),
		float2(-0.098422, -0.295755),
		float2(-0.885922, 0.215369),
		float2(0.566637, 0.605213),
		float2(0.039766, -0.396100),
		float2(0.751946, 0.453352),
		float2(0.078707, -0.715323),
		float2(-0.075838, -0.529344),
		float2(0.724479, -0.580798),
		float2(0.222999, -0.215125),
		float2(-0.467574, -0.405438),
		float2(-0.248268, -0.814753),
		float2(0.354411, -0.887570),
		float2(0.175817, 0.382366),
		float2(0.487472, -0.063082),
		float2(-0.084078, 0.898312),
		float2(0.488876, -0.783441),
		float2(0.470016, 0.217933),
		float2(-0.696890, -0.549791),
		float2(-0.149693, 0.605762),
		float2(0.034211, 0.979980)
	};

	float2 sampleOffset = poissonDisk32[index] * sD.ShadowSampleParam.z;

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

		if (cascadeIndex < 1 && sD.StartSplitDistances.y < shadowMapDepth) {
			float cascade1BlendFactor = smoothstep(0, 1, (shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));

			if (cascade1BlendFactor > (float(index) / 31)){
				lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][1];
				shadowMapThreshold = sD.AlphaTestRef.z;
				cascadeIndex = 1;
			}
		}

		float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;

		positionLS.xy += sampleOffset * rcp(1.0 + cascadeIndex);

		return SharedShadowMap.SampleCmpLevelZero(comparisonSampler, float3(positionLS.xy, cascadeIndex), positionLS.z - shadowMapThreshold);
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


	float3 viewDirection = FrameBuffer::WorldToView(-normalize(cellCentreMS), false);
	float2 uv = FrameBuffer::ViewToUV(viewDirection, false);

	if (!FrameBuffer::IsOutsideFrame(uv) && viewDirection.z < 0.0) {  // Check that the view direction exists in screenspace and that it is in front of the camera
		uint shadowVisibilityBitShift = outShadowVisibilityBitShiftArray[dtid];
		uint hasShadowVisibility = uint(Get2DFilteredShadow(cellCentreMS, shadowVisibilityBitShift, 0));

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