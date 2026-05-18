#include "Common/FrameBuffer.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#define LinearSampler defaultSampler
SamplerState defaultSampler : register(s0);

#include "Common/ShadowSampling.hlsli"

struct VS_OUTPUT_POST
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

float main(VS_OUTPUT_POST input) : SV_Target0
{
	float2 uv = input.txcoord0;
	uint eyeIndex = 0;

	float depth = SharedData::GetDepth(uv);
	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
	positionMS.xyz /= positionMS.w;

	float extinction = SharedData::enbSettings.VolumetricRaysExtinction;
	float totalRayLength = length(positionMS.xyz);

	const uint sampleCount = 16;
	float noise = Random::InterleavedGradientNoise(input.pos.xy, SharedData::FrameCount);
	float3 cameraOffset = FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

	float scattering = 0.0;
	float transmittance = 1.0;

	for (uint i = 0; i < sampleCount; i++) {
		float t0 = float(i) / float(sampleCount);
		float t1 = float(i + 1) / float(sampleCount);

		t0 *= t0;
		t1 *= t1;

		float t = lerp(t0, t1, noise);
		float stepLength = (t1 - t0) * totalRayLength;

		float3 samplePos = positionMS.xyz * t;
		float shadow = ShadowSampling::GetTrueWorldShadow(samplePos, cameraOffset, eyeIndex);

		float stepTransmittance = exp(-extinction * stepLength);
		scattering += shadow * (1.0 - stepTransmittance) * transmittance;
		transmittance *= stepTransmittance;
	}

	return scattering;
}
