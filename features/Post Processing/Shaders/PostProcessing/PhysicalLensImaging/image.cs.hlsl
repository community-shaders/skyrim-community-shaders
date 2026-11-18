Texture2D<float4> Scene : register(t0);
Texture2DArray<float4> CameraMaps : register(t1);
#include "PostProcessing/PhysicalLensImaging/mapping.hlsli"
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> Image : register(u0);
cbuffer ImageParameters : register(b0)
{
	uint Width, Height, GridWidth, GridHeight;
};

[numthreads(8, 8, 1)] void RenderImage(uint3 id : SV_DispatchThreadID) {
	if (id.x >= Width || id.y >= Height)
		return;
	float2 uv = (float2(id.xy) + .5) / float2(Width, Height);
	float2 greenUV;
	float3 color = SamplePhysicalLensImage(Scene, CameraMaps, LinearClamp, uv, greenUV);
	Image[id.xy] = float4(color, 1);
}
