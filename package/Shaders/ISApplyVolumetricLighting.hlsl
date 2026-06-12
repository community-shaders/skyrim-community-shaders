#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float VL: SV_Target0;
};

#if defined(PSHADER)
SamplerState DepthSampler : register(s0);
SamplerState PreviousFrameSampler : register(s1);
SamplerState VLSampler : register(s2);
SamplerState RepartitionSampler : register(s3);
SamplerState NoiseGradSamplerSampler : register(s4);
SamplerState MotionVectorsSampler : register(s5);
SamplerState PreviousDepthSampler : register(s6);

Texture2D<float4> DepthTex : register(t0);
Texture2D<float4> PreviousFrameTex : register(t1);
Texture3D<float4> VLTex : register(t2);
Texture1D<float4> RepartitionTex : register(t3);
Texture2D<float4> NoiseGradSamplerTex : register(t4);
Texture2D<float4> MotionVectorsTex : register(t5);
Texture2D<float4> PreviousDepthTex : register(t6);

cbuffer PerGeometry : register(b2)
{
	float4 g_IntensityX_TemporalY : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	static const float kVLThresholdBias = 1.0 / 128.0;

	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float depth = DepthTex.Sample(DepthSampler, screenPosition).x;

	float repartition = clamp(RepartitionTex.SampleLevel(RepartitionSampler, depth, 0).x, 0, 0.9999);
	float vl = g_IntensityX_TemporalY.x * VLTex.SampleLevel(VLSampler, float3(input.TexCoord, repartition), 0).x;

	float noiseGrad = 0.03125 * NoiseGradSamplerTex.Sample(NoiseGradSamplerSampler, 0.125 * input.Position.xy).x;

	float adjustedVl = max(0.0, noiseGrad + vl - kVLThresholdBias);

	if (0.001 < g_IntensityX_TemporalY.y) {
		float2 motionVector = MotionVectorsTex.Sample(MotionVectorsSampler, screenPosition).xy;
		float2 previousTexCoord = input.TexCoord + motionVector;
		bool isValid = previousTexCoord.x >= 0 && previousTexCoord.x < 1 && previousTexCoord.y >= 0 && previousTexCoord.y < 1;
		float2 previousScreenPosition = FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(previousTexCoord);
		float previousVl = PreviousFrameTex.Sample(PreviousFrameSampler, previousScreenPosition).x;
		float previousDepth = PreviousDepthTex.Sample(PreviousDepthSampler, previousScreenPosition).x;

		float temporalContribution = g_IntensityX_TemporalY.y * (1 - smoothstep(0, 1, min(1, 100 * abs(depth - previousDepth))));
		psout.VL = lerp(adjustedVl, previousVl, temporalContribution * isValid);
	} else {
		psout.VL = adjustedVl;
	}

	return psout;
}
#endif
