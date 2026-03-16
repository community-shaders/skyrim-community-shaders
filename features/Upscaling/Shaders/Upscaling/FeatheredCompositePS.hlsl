// Feathered DLSS crop composite using hardware alpha blending.
// Based on PureDark's approach from Skyrim-Upscaler VR (MIT license).
//
// The render target already contains TAA'd periphery content.
// We output float4(DLSSColor, featherAlpha) and let the output merger's
// SrcAlpha/InvSrcAlpha blend preserve the periphery in the feather zone
// and outside the crop rect entirely.

#include "Upscaling/UpscaleVS.hlsl"

#ifdef PSHADER

Texture2D<float4> CropTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer FeatheredCompositeCB : register(b0)
{
	float2 CropOrigin;    // paste position (x, y) in output-eye pixel coords
	float2 CropSize;      // crop width, height in pixels
	float FeatherWidth;   // feather distance in pixels (inward from crop edge)
	float _pad0;
	float2 SrcUVOrigin;   // UV origin in source texture for this crop region
	float2 SrcUVScale;    // UV scale: maps [0,1] crop-local UV to source texture UV range
};

float4 main(VS_OUTPUT input) : SV_Target
{
	float2 pixelPos = input.Position.xy;

	// Distance from each edge of the crop rect (positive = inside)
	float distLeft   = pixelPos.x - CropOrigin.x;
	float distRight  = (CropOrigin.x + CropSize.x) - pixelPos.x;
	float distTop    = pixelPos.y - CropOrigin.y;
	float distBottom = (CropOrigin.y + CropSize.y) - pixelPos.y;

	float minDist = min(min(distLeft, distRight), min(distTop, distBottom));

	// Outside crop rect: fully transparent (hardware blend preserves TAA'd periphery)
	if (minDist <= 0.0)
		return float4(0, 0, 0, 0);

	// Feather alpha: smoothstep ramp from 0 at edge to 1 at FeatherWidth inside
	// (matches the smoothstep from the original CS for visual consistency)
	float alpha = (FeatherWidth > 0.0) ? smoothstep(0.0, FeatherWidth, minDist) : 1.0;

	// Map pixel position to crop-local UV [0,1], then remap to source texture UV.
	// For per-eye textures: SrcUVOrigin=(0,0), SrcUVScale=(1,1) (identity).
	// For SBS textures: SrcUVOrigin/Scale select the correct eye's crop region.
	float2 cropUV = (pixelPos - CropOrigin) / CropSize;
	float2 srcUV = cropUV * SrcUVScale + SrcUVOrigin;
	float3 dlssColor = CropTexture.SampleLevel(LinearSampler, srcUV, 0).rgb;

	return float4(dlssColor, alpha);
}

#endif  // PSHADER
