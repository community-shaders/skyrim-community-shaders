#include "PostProcessing/fullscreen.hlsli"

cbuffer CameraCB : register(b1)
{
	float4 CameraScreen;
	float4 CameraLens;
	float4 CameraAberration;
	float4 CameraSoftness;
	float4 CameraGrain;
}

Texture2D<float4> InputTexture : register(t0);
SamplerState ColorSampler : register(s0);

float2 CameraAspectScale()
{
	return float2(CameraLens.y, 1.0);
}

float CameraNormalizedRadius(float2 uv)
{
	return length((uv - 0.5) * CameraAspectScale()) / CameraLens.z;
}
