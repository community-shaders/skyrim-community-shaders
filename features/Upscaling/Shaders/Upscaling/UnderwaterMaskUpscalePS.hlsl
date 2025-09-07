#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float UnderwaterMask : SV_TARGET;
};

SamplerState LinearSampler : register(s0);

Texture2D<float> UnderwaterMask : register(t0);

cbuffer JitterCB : register(b0)
{
	float2 jitter;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);
	
	// Clamp within bounds
	uv.x = clamp(uv.x, 0.0, originalUV.x - SharedData::BufferDim.zw);
	uv.y = clamp(uv.y, 0.0, originalUV.y - SharedData::BufferDim.zw);

	// Upscale using linear sampling with jitter-corrected coordinates
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif