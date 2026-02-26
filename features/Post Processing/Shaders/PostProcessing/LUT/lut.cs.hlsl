
#include "Common/Color.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float3> TexColor : register(t0);
Texture2D<float3> TexLut : register(t1);
Texture3D<float3> TexLut3D : register(t2);

cbuffer LUTCB : register(b1)
{
	float3 InputMin : packoffset(c0.x);
	float pad : packoffset(c0.w);
	float3 InputMax : packoffset(c1.x);
	int LutType : packoffset(c1.w);
};

// x -> y -> z
float3 biLerp(in float3 values[8], in float3 lerpFactors)
{
	float3 x1 = lerp(values[0], values[1], lerpFactors.x);
	float3 x2 = lerp(values[2], values[3], lerpFactors.x);
	float3 x3 = lerp(values[4], values[5], lerpFactors.x);
	float3 x4 = lerp(values[6], values[7], lerpFactors.x);
	float3 y1 = lerp(x1, x2, lerpFactors.y);
	float3 y2 = lerp(x3, x4, lerpFactors.y);
	float3 z = lerp(y1, y2, lerpFactors.z);
	return z;
}

[numthreads(8, 8, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	uint3 dims;
	[branch] if (LutType == 3)
		TexLut3D.GetDimensions(dims.x, dims.y, dims.z);
	else TexLut.GetDimensions(dims.x, dims.y);

	float3 color = TexColor[tid].rgb;
	[branch] if (LutType == 0)
	{
		float luma = Color::RGBToLuminance(color);
		float pxCoord = (luma - InputMin.x) / (InputMax.x - InputMin.x) * (dims.x - 1);
		int px0 = clamp(int(pxCoord), 0, dims.x - 1);
		int px1 = min(px0 + 1, dims.x - 1);
		float targetLuma = lerp(TexLut[int2(px0, 1)].x, TexLut[int2(px1, 1)].x, saturate(pxCoord - px0));

		color *= targetLuma / (luma + 1e-8);
	}
	else if (LutType == 1)
	{
		float3 pxCoord = (color - InputMin) / (InputMax - InputMin) * (dims.x - 1);
		int3 px0 = clamp(int3(pxCoord), 0, dims.x - 1);
		int3 px1 = min(px0 + 1, dims.x - 1);
		float3 lerpFactors = saturate(pxCoord - px0);

		color.r = lerp(TexLut[int2(px0.x, 1)].x, TexLut[int2(px1.x, 1)].x, lerpFactors.x);
		color.g = lerp(TexLut[int2(px0.y, 1)].x, TexLut[int2(px1.y, 1)].x, lerpFactors.y);
		color.b = lerp(TexLut[int2(px0.z, 1)].x, TexLut[int2(px1.z, 1)].x, lerpFactors.z);
	}
	else
	{
		dims = LutType == 2 ? uint3(dims.y, dims.y, dims.x / dims.y) : dims;

		float3 pxCoord = (color - InputMin) / (InputMax - InputMin) * (dims - 1);
		int3 px0 = clamp(int3(pxCoord), 0, dims - 1);
		int3 px1 = min(px0 + 1, dims - 1);
		float3 lerpFactors = saturate(pxCoord - px0);

		float3 lutSamples[8];
		[branch] if (LutType == 2)
		{
			lutSamples[0] = TexLut[int2(px0.x + dims.y * px0.z, px0.y)];
			lutSamples[1] = TexLut[int2(px1.x + dims.y * px0.z, px0.y)];
			lutSamples[2] = TexLut[int2(px0.x + dims.y * px0.z, px1.y)];
			lutSamples[3] = TexLut[int2(px1.x + dims.y * px0.z, px1.y)];
			lutSamples[4] = TexLut[int2(px0.x + dims.y * px1.z, px0.y)];
			lutSamples[5] = TexLut[int2(px1.x + dims.y * px1.z, px0.y)];
			lutSamples[6] = TexLut[int2(px0.x + dims.y * px1.z, px1.y)];
			lutSamples[7] = TexLut[int2(px1.x + dims.y * px1.z, px1.y)];
		}
		else
		{
			lutSamples[0] = TexLut3D[px0];
			lutSamples[1] = TexLut3D[int3(px1.x, px0.y, px0.z)];
			lutSamples[2] = TexLut3D[int3(px0.x, px1.y, px0.z)];
			lutSamples[3] = TexLut3D[int3(px1.x, px1.y, px0.z)];
			lutSamples[4] = TexLut3D[int3(px0.x, px0.y, px1.z)];
			lutSamples[5] = TexLut3D[int3(px1.x, px0.y, px1.z)];
			lutSamples[6] = TexLut3D[int3(px0.x, px1.y, px1.z)];
			lutSamples[7] = TexLut3D[px1];
		}

		color = biLerp(lutSamples, lerpFactors);
	}

	RWTexOut[tid] = float4(color, 1);
}