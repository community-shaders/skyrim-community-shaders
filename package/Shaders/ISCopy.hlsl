#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
#if defined(DEPTHBUFFER_COPY)
	float Depth : SV_Depth;
#else
	float4 Color : SV_Target0;
#endif
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);

Texture2D<float4> ImageTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 RotationMatrix : packoffset(c0);
	float4 ColorSelect : packoffset(c1);
	float4 ScaleBias : packoffset(c2);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if !defined(DISABLE_DYNAMIC) || (defined(DEPTHBUFFER_COPY) && defined(DEPTHBUFFER_4X_DOWNSAMPLE))
	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
#	else
	float2 screenPosition = input.TexCoord;
#	endif

	float4 color = ImageTex.Sample(ImageSampler, screenPosition);

#	if defined(GREYSCALE)
	color = float4(dot(color, ColorSelect).xxx, color.w);
#	elif defined(MASK)
	color.w = 1 - color.x;
#	endif

#	if defined(DEPTHBUFFER_COPY)
	psout.Depth = color.x;
#	else
	psout.Color = color;
#	endif

	return psout;
}
#endif
