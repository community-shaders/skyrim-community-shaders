// Offline application harness for a compact core and finite-source tails.
#include "PostProcessing/PhysicalLensImaging/diffraction_sampling.hlsli"
Texture2D<float4> Scene : register(t0);
Texture2DArray<float4> DiffractionPSF : register(t1);
StructuredBuffer<float4> CoreWeights : register(t2);
StructuredBuffer<float4> SourceRegions : register(t3);
struct DiffractionSource
{
	float4 Color, Position;
};
StructuredBuffer<DiffractionSource> Sources : register(t4);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> Image : register(u0);
cbuffer ImageParameters : register(b0)
{
	uint Width, Height, SourceCount, Unused;
};
#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"

[numthreads(8, 8, 1)] void ApplyDiffraction(uint3 id : SV_DispatchThreadID) {
	if (id.x >= Width || id.y >= Height)
		return;
	float2 pixels = float2(Width, Height), uv = (float2(id.xy) + .5) / pixels;
	float3 color = 0;
	[unroll] for (uint band = 0; band < 3; ++band)
	{
		color[band] = SampleDiffractionCore(Scene, CoreWeights, SourceRegions, CoreWeights, LinearClamp, uv, float2(1. / Width, 0), float2(0, 1. / Height), band, false);
		for (uint source = 0; source < SourceCount; ++source) {
			DiffractionSource light = Sources[source];
			float tail = SampleDiffractionBox(DiffractionPSF, LinearClamp, (uv - light.Position.xy) * pixels, max(sqrt(3 * light.Position.zw * pixels * pixels), .5), WaveSampling[band].xy, band);
			color[band] += light.Color[band] * Width * Height * tail;
		}
	}
	Image[id.xy] = float4(color, 1);
}
