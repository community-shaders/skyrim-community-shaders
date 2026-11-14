#include "SnowDeformation/SnowNoise.hlsli"

cbuffer UndulationFieldCB : register(b0)
{
	float2 FieldOriginWorld;
	float FieldTexel;
	float FieldScale;
}

RWTexture2D<float4> UndulationField : register(u0);
RWTexture2D<unorm float> NoiseHashTable : register(u1);

[numthreads(8, 8, 1)] void NoiseHashCS(uint3 id : SV_DispatchThreadID) {
	NoiseHashTable[id.xy] = ShapeNoiseHash(float2(id.xy));
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	float2 w = FieldOriginWorld + float2(id.xy) * FieldTexel;
	const float uStep = kUndulationGradStep;
	float h = UndulationNorm(w, FieldScale);
	float2 g = float2(
					UndulationNorm(w + float2(uStep, 0.0), FieldScale) - UndulationNorm(w - float2(uStep, 0.0), FieldScale),
					UndulationNorm(w + float2(0.0, uStep), FieldScale) - UndulationNorm(w - float2(0.0, uStep), FieldScale)) /
	           (2.0 * uStep);
	UndulationField[id.xy] = float4(h, g, 0.0);
}
