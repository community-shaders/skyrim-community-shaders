// AMD Capsaicin GI-1.1 — GI Denoiser helpers
// Ported from Capsaicin gi_denoiser.hlsl

#ifndef GI_DENOISER_HLSLI
#define GI_DENOISER_HLSLI

#define kGIDenoiser_MaxBlurMask 8.0f

// Retrieves the radius for the blur kernel from the blur mask texture
int GIDenoiser_GetBlurRadius(in RWTexture2D<float> blurMask, in uint2 pos)
{
	int blur_radius = 0;
	float blur_mask = blurMask[pos] * kGIDenoiser_MaxBlurMask;

	if (blur_mask > 0.0f)
	{
		blur_radius = int(max(blur_mask, 1.0f) + 0.5f);
	}

	return blur_radius;
}

// Removes NaNs from color values via Reinhard-style clamp
float GIDenoiser_RemoveNaNs(in float color)
{
	color /= (1.0f + color);
	color = saturate(color);
	color /= max(1.0f - color, 1e-4f);
	return color;
}

float3 GIDenoiser_RemoveNaNs(in float3 color)
{
	color /= (1.0f + color);
	color = saturate(color);
	color /= max(1.0f - color, 1e-4f);
	return color;
}

float4 GIDenoiser_RemoveNaNs(in float4 color)
{
	color /= (1.0f + color);
	color = saturate(color);
	color /= max(1.0f - color, 1e-4f);
	return color;
}

#endif  // GI_DENOISER_HLSLI
