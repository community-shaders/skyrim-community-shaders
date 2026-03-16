// Format-converting fullscreen pixel shader with optional bilinear upscale.
// Used by TAAReorder to composite between textures of different DXGI formats
// (e.g. R8G8B8A8_UNORM conductor RTs <-> R11G11B10_FLOAT kMAIN).
// The GPU's output merger handles format conversion automatically.
//
// BILINEAR_UPSCALE variant: upscales render-res content to display-res by
// mapping output pixel positions through the dynamic resolution scale,
// like PureDark's dynamicResScale in his blend shader.

#include "Upscaling/UpscaleVS.hlsl"

#ifdef PSHADER

Texture2D<float4> Source : register(t0);

#ifdef BILINEAR_UPSCALE

cbuffer CompositeCB : register(b0)
{
	float2 DynResScale;  // renderRes / displayRes (per-eye)
	float2 EyeOffset;   // (i * eyeWidth, 0) in texels
	float2 SrcTexSize;  // full texture dimensions in texels
	float2 pad;
};

SamplerState LinearSampler : register(s0);

float4 main(VS_OUTPUT input) : SV_Target
{
	// Map display-res pixel position to render-res source position.
	// Subtract eye offset, scale to render-res, add eye offset back.
	float2 localPos = input.Position.xy - EyeOffset;
	float2 srcLocal = localPos * DynResScale;
	float2 srcPos = srcLocal + EyeOffset;
	float2 srcUV = srcPos / SrcTexSize;
	return Source.SampleLevel(LinearSampler, srcUV, 0);
}

#else

float4 main(VS_OUTPUT input) : SV_Target
{
	return Source.Load(int3(input.Position.xy, 0));
}

#endif  // BILINEAR_UPSCALE

#endif  // PSHADER
