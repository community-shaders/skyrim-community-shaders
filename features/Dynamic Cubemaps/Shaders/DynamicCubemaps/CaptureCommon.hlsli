#ifndef DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI
#define DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI

#include "Common/Color.hlsli"
#include "Common/ColorManagement.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "DynamicCubemaps/CubemapCommon.hlsli"

RWTexture2DArray<float4> DynamicCubemap : register(u0);
RWTexture2DArray<float4> DynamicCubemapRaw : register(u1);
RWTexture2DArray<float4> DynamicCubemapPosition : register(u2);

struct CaptureLightingState
{
	float ReferenceLuminance;
	uint PendingResetMask;
	uint Reset;
	uint Initialized;
};

RWStructuredBuffer<CaptureLightingState> LightingState : register(u3);

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);
SamplerState LinearSampler : register(s0);

cbuffer UpdateData : register(b0)
{
	float3 CameraPreviousPosAdjust2;
	uint CaptureIndex;
	float CaptureDeltaTime;
	uint ResetCapture;
	uint2 UpdatePadding;
}

static const float CaptureHistoryLifetime = 30.0;

float GetCaptureTime()
{
	return floor(fmod(SharedData::Timer, 64.0) * 16.0) / 16.0;
}

bool CaptureHistoryExpired(float lastSeen)
{
	return CaptureDeltaTime >= CaptureHistoryLifetime || fmod(GetCaptureTime() - lastSeen + 64.0, 64.0) >= CaptureHistoryLifetime;
}

float3 GetSamplingVector(uint3 texel)
{
	uint width, height, faces;
	DynamicCubemap.GetDimensions(width, height, faces);
	return GetCubemapSamplingVector(texel, float2(width, height));
}

bool SampleCapture(uint3 texel, out float3 position, out float3 color, out float2 uv)
{
	position = 0.0;
	color = 0.0;
	float3 viewDirection = FrameBuffer::WorldToView(-GetSamplingVector(texel), false);
	uv = FrameBuffer::ViewToUV(viewDirection, false);
	if (viewDirection.z >= 0.0 || !all(isfinite(uv)) || FrameBuffer::IsOutsideFrame(uv))
		return false;

	float2 sampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
#if defined(REFLECTIONS)
	bool excludeSky = false;
#else
	bool excludeSky = true;
#endif
	if (excludeSky) {
		float4 depths = DepthTexture.GatherRed(LinearSampler, sampleUV);
		if (any(depths >= 1.0))
			return false;
	}
	float depth = DepthTexture.SampleLevel(LinearSampler, sampleUV, 0);
	if (SharedData::GetScreenDepth(depth) <= 16.5)
		return false;

	float4 positionCS = mul(FrameBuffer::CameraViewProjInverse, float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
	position = positionCS.xyz / positionCS.w * 0.001;
	color = ColorManagement::SceneToLinear(ColorTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb);
	if (!all(isfinite(position)) || !all(isfinite(color)))
		return false;
	color = clamp(color, 0.0, 65504.0);
	return true;
}

float3 AdjustCapturePosition(float3 position)
{
	return position + (CameraPreviousPosAdjust2 - FrameBuffer::CameraPosAdjust.xyz) * 0.001;
}

bool CaptureGeometryMatches(float3 previousPosition, float3 position)
{
	return length(previousPosition - position) <= max(0.032, length(position) * 0.03);
}

#endif
