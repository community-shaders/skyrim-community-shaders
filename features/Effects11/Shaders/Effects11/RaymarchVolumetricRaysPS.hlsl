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

struct PS_OUTPUT
{
	float Scattering : SV_Target0;
	// Depth this texel raymarched with; the bilateral blur + upsample weight against it.
	float Depth : SV_Target1;
};

PS_OUTPUT main(VS_OUTPUT_POST input)
{
	float2 uv = input.txcoord0;

	float depth = SharedData::GetDepth(uv);
	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse, positionCS);
	positionMS.xyz /= positionMS.w;

	float extinction = SharedData::enbSettings.VolumetricRaysExtinction;
	float totalRayLength = length(positionMS.xyz);

	const uint sampleCount = 16;
	const float rcpSampleCount = 1.0 / float(sampleCount);
	float noise = Random::InterleavedGradientNoise(input.pos.xy, SharedData::FrameCount);
	float3 cameraOffset = FrameBuffer::CameraPosAdjust.xyz;
	float negExtTimesRayLen = -extinction * totalRayLength;

	float scattering = 0.0;
	float transmittance = 1.0;

	[unroll]
	for (uint i = 0; i < sampleCount; i++) {
		float t0 = float(i) * rcpSampleCount;
		float t1 = float(i + 1) * rcpSampleCount;

		t0 *= t0;
		t1 *= t1;

		float t = lerp(t0, t1, noise);
		float stepDelta = t1 - t0;

		float3 samplePos = positionMS.xyz * t;

		float shadow = 1.0;
		
#if defined(TERRAIN_SHADOWS)
		shadow = TerrainShadows::GetTerrainShadow(samplePos + cameraOffset, LinearSampler);
#endif

#if defined(CLOUD_SHADOWS)
		shadow *= CloudShadows::GetCloudShadowMult(samplePos, LinearSampler);
#endif
		shadow *= shadow;

		float stepTransmittance = exp(negExtTimesRayLen * stepDelta);
		scattering += shadow * (1.0 - stepTransmittance) * transmittance;
		transmittance *= stepTransmittance;
	}

	PS_OUTPUT output;
	output.Scattering = scattering;
	output.Depth = depth;
	return output;
}
