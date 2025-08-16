#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 UnderwaterMask : SV_TARGET;
};

SamplerState LinearSampler : register(s0);

Texture2D<float4> UnderwaterMask : register(t0);

cbuffer JitterCB : register(b0)
{
	float2 jitter;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	
	// Remove jitter offset to get the correct sampling coordinates
	uv -= jitter * SharedData::BufferDim.zw;

	// Upscale using linear sampling with jitter-corrected coordinates
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif