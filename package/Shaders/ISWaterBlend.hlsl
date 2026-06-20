#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color: SV_Target0;
	float4 Color1: SV_Target1;
};

#if defined(PSHADER)
SamplerState sourceSampler : register(s0);
SamplerState waterHistorySampler : register(s1);
SamplerState motionBufferSampler : register(s2);
SamplerState depthBufferSampler : register(s3);
SamplerState waterMaskSampler : register(s4);

Texture2D<float4> sourceTex : register(t0);
Texture2D<float4> waterHistoryTex : register(t1);
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
	static const float FullWaterCoverageThreshold = 1e-3f;

	float GetWaterCoverage(float waterMask)
	{
		return saturate((waterMask - WaterMaskThreshold) / (FullWaterCoverageThreshold - WaterMaskThreshold));
	}
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float2 adjustedScreenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float waterMask = waterMaskTex.Sample(waterMaskSampler, adjustedScreenPosition).z;
	if (waterMask < WaterBlend::WaterMaskThreshold) {
		discard;
	}

	float3 sourceColor = sourceTex.Sample(sourceSampler, adjustedScreenPosition).xyz;
	float2 motion = motionBufferTex.Sample(motionBufferSampler, adjustedScreenPosition).xy;
	float2 motionScreenPosition = input.TexCoord + motion;
	float2 motionAdjustedScreenPosition =
		FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(motionScreenPosition);
	float4 waterHistory =
		waterHistoryTex.Sample(waterHistorySampler, motionAdjustedScreenPosition).xyzw;

	float3 finalColor = sourceColor;
	if (motionScreenPosition.x >= 0 && motionScreenPosition.y >= 0 && motionScreenPosition.x <= 1 &&
		motionScreenPosition.y <= 1 && waterHistory.w > 0.0 && all(isfinite(waterHistory))) {
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
		// Un-premultiply history so bilinear filtering against cleared pixels does not darken water edges
		float3 historyColor = waterHistory.xyz / max(waterHistory.w, EPSILON_DIVISION);

		historyFactor *= waterHistory.w;
		finalColor = lerp(sourceColor, historyColor, historyFactor);
	}

	float waterCoverage = WaterBlend::GetWaterCoverage(waterMask);
	// Store premultiplied history so transparent clears filter without dark outlines
	psout.Color1 = float4(finalColor * waterCoverage, waterCoverage);
	psout.Color = finalColor;

	return psout;
}
#endif
