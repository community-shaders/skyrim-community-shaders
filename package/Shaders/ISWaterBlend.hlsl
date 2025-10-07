#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color : SV_Target0;
	float4 Color1 : SV_Target1;
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

float3 LogToLinear(float3 logColor)
{
    const float linearRange = 14.0f;
    const float linearGrey = 0.18f;
    const float exposureGrey = 444.0f;
    return exp2((logColor - exposureGrey / 1023.0) * linearRange) * linearGrey;
}

float3 LinearToLog(float3 linearColor)
{
    const float linearRange = 14.0f;
    const float linearGrey = 0.18f;
    const float exposureGrey = 444.0f;
    return saturate(log2(linearColor) / linearRange - log2(linearGrey) / linearRange + exposureGrey / 1023.0f);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(input.TexCoord);
	float2 adjustedScreenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float waterMask = waterMaskTex.Sample(waterMaskSampler, adjustedScreenPosition).z;
	if (waterMask < 1e-4) {
		discard;
	}

	float3 sourceColor = sourceTex.Sample(sourceSampler, adjustedScreenPosition).xyz;
	float2 motion = motionBufferTex.Sample(motionBufferSampler, adjustedScreenPosition).xy;
	float2 motionScreenPosition = Stereo::ConvertToStereoUV(Stereo::ConvertFromStereoUV(input.TexCoord, eyeIndex) + motion, eyeIndex);
	float2 motionAdjustedScreenPosition =
		FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(motionScreenPosition);
	float4 waterHistory =
		waterHistoryTex.Sample(waterHistorySampler, motionAdjustedScreenPosition).xyzw;
	waterHistory.xyz = LogToLinear(waterHistory.xyz) - LogToLinear(0);

	float3 finalColor = sourceColor;
	if (
#	ifndef VR
		motionScreenPosition.x >= 0 && motionScreenPosition.y >= 0 && motionScreenPosition.x <= 1 &&
#	endif
		motionScreenPosition.y <= 1 && waterHistory.w == 1) {
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
		finalColor = lerp(sourceColor, waterHistory.xyz, historyFactor);
	}

	psout.Color1 = float4(LinearToLog(finalColor + LogToLinear(0)), 1);
	psout.Color = finalColor;

	return psout;
}
#endif
