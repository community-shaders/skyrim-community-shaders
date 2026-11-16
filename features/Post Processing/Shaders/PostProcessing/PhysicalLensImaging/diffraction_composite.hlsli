#define WAVE_PARAMETER_REGISTER b4
#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"
#include "PostProcessing/PhysicalLensImaging/diffraction_sampling.hlsli"

StructuredBuffer<float4> TexWaveKernel : register(t10);
StructuredBuffer<float4> TexWaveRegions : register(t11);
StructuredBuffer<float4> TexWaveSources : register(t12);
StructuredBuffer<uint> TexWaveTiles : register(t13);
Texture2DArray<float> TexWaveTail : register(t14);

float3 SamplePhysicalLensWave(Texture2D<float4> scene, Texture2DArray<float4> mappingTexture,
	SamplerState linearSampler, float2 uv, out float2 greenUV)
{
	uint2 tile = min(uint2(uv * WaveImageSize) / 16, (WaveImageSize - 1) / 16);
	uint flags = TexWaveTiles[tile.y * ((WaveImageSize.x + 15) / 16) + tile.x];
	float3 color = 0;
	greenUV = uv;
	[unroll] for (uint band = 0; band < 3; ++band)
	{
		float3 mapping = float3(uv, 1);
		[branch] if (DiffractionEnabled > 1)
		{
			uint w, h, layers;
			mappingTexture.GetDimensions(w, h, layers);
			float2 tableUV = (uv * float2(w - 1, h - 1) + .5) / float2(w, h);
			mapping = mappingTexture.SampleLevel(linearSampler, float3(tableUV, band), 0).xyz;
		}
		float2 dx = ddx(mapping.xy), dy = ddy(mapping.xy);
		if (band == 1)
			greenUV = mapping.xy;
		if (all(mapping.xy >= 0) && all(mapping.xy <= 1)) {
			float value;
			[branch] if ((flags & (1u << 16)) != 0) value = SampleDiffractionCore(scene, TexWaveKernel, TexWaveRegions, TexWaveSources, linearSampler, mapping.xy, dx, dy, band, true);
			else value = scene.SampleLevel(linearSampler, mapping.xy, 0)[band];
			color[band] = value * mapping.z;
		}
	}
	[branch] if ((flags & 0xffff) != 0)
	{
		uint2 p = uint2(uv * WaveImageSize);
		color += float3(TexWaveTail.Load(int4(p, 0, 0)), TexWaveTail.Load(int4(p, 1, 0)), TexWaveTail.Load(int4(p, 2, 0)));
	}
	return color;
}
