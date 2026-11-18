#ifndef PHYSICAL_LENS_MAPPING
#define PHYSICAL_LENS_MAPPING
// Full-resolution resampling: retain sharp scene detail; no DoF or PSF blur.
float3 SamplePhysicalLensImage(Texture2D<float4> scene, Texture2DArray<float4> mappingTexture,
	SamplerState linearSampler, float2 uv, out float2 greenUV)
{
	uint width, height, bands;
	mappingTexture.GetDimensions(width, height, bands);
	float2 tableUV = (uv * float2(width - 1, height - 1) + .5) / float2(width, height);
	float3 color = 0;
	greenUV = uv;
	[unroll] for (uint band = 0; band < 3; ++band)
	{
		float4 mapping = mappingTexture.SampleLevel(linearSampler, float3(tableUV, band), 0);
		if (band == 1)
			greenUV = mapping.xy;
		if (all(mapping.xy >= 0) && all(mapping.xy <= 1))
			color[band] = scene.SampleLevel(linearSampler, mapping.xy, 0)[band] * mapping.z;
	}
	return color;
}
#endif
