#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/VR.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color: SV_Target0;
	float3 Color1: SV_Target1;
};

#if defined(PSHADER)
SamplerState sourceSampler : register(s0);
SamplerState waterHistorySampler : register(s1);
SamplerState motionBufferSampler : register(s2);
SamplerState depthBufferSampler : register(s3);
SamplerState waterMaskSampler : register(s4);

Texture2D<float4> sourceTex : register(t0);
Texture2D<float3> waterHistoryTex : register(t1);
Texture2D<float4> motionBufferTex : register(t2);
Texture2D<float4> depthBufferTex : register(t3);
Texture2D<float4> waterMaskTex : register(t4);

cbuffer PerGeometry : register(b2)
{
	float4 NearFar_Menu_DistanceFactor : packoffset(c0);
};

namespace WaterBlend
{
	static const float WaterMaskThreshold = 1e-4f;
	static const float FullHistoryCoverageThreshold = 1e-3f;

	float SaturateRange(float value, float minValue, float maxValue)
	{
		return saturate((value - minValue) / max(maxValue - minValue, EPSILON_DIVISION));
	}

	float GetHistoryCoverage(float historyMask)
	{
		return SaturateRange(historyMask, WaterMaskThreshold, FullHistoryCoverageThreshold);
	}
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(input.TexCoord);
	float2 adjustedScreenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float waterMask = waterMaskTex.Sample(waterMaskSampler, adjustedScreenPosition).z;
	if (waterMask < WaterBlend::WaterMaskThreshold) {
		discard;
	}

	float3 sourceColor = sourceTex.Sample(sourceSampler, adjustedScreenPosition).xyz;
	float2 motion = motionBufferTex.Sample(motionBufferSampler, adjustedScreenPosition).xy;
	float2 motionScreenPosition = Stereo::ConvertToStereoUV(Stereo::ConvertFromStereoUV(input.TexCoord, eyeIndex) + motion, eyeIndex);
	float2 motionAdjustedScreenPosition =
		FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(motionScreenPosition);

	float historyMask = waterMaskTex.Sample(waterMaskSampler, motionAdjustedScreenPosition).z;
	float historyCoverage = WaterBlend::GetHistoryCoverage(historyMask);
	float3 finalColor = sourceColor;
	if (
#	ifndef VR
		motionScreenPosition.x >= 0 && motionScreenPosition.y >= 0 && motionScreenPosition.x <= 1 &&
#	endif
		motionScreenPosition.y <= 1 && historyCoverage > 0.0) {
		float3 waterHistory = waterHistoryTex.Sample(waterHistorySampler, motionAdjustedScreenPosition).xyz;
		float historyFactor = 0.95;
		if (NearFar_Menu_DistanceFactor.z == 0) {
			float depth = depthBufferTex.Sample(depthBufferSampler, adjustedScreenPosition).x;
			float distanceFactor = clamp(250 * ((-NearFar_Menu_DistanceFactor.x +
													(2 * NearFar_Menu_DistanceFactor.x * NearFar_Menu_DistanceFactor.y) /
														(-(depth * 2 - 1) *
																(NearFar_Menu_DistanceFactor.y - NearFar_Menu_DistanceFactor.x) +
															(NearFar_Menu_DistanceFactor.y + NearFar_Menu_DistanceFactor.x))) /
												   (NearFar_Menu_DistanceFactor.y - NearFar_Menu_DistanceFactor.x)),
				0.1, 0.95);
			historyFactor = NearFar_Menu_DistanceFactor.w * (distanceFactor * (waterMask * -0.85 + 0.95));
		}
		historyFactor *= historyCoverage;
		finalColor = lerp(sourceColor, waterHistory.xyz, historyFactor);
	}

	psout.Color1 = finalColor;
	psout.Color = finalColor;

	return psout;
}
#endif
