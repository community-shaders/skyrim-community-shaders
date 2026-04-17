// AMD Capsaicin GI-1.1 — Halton low-discrepancy sequence
// Ported from Capsaicin gi1.hlsl

#ifndef HALTON_HLSLI
#define HALTON_HLSLI

float CalculateHaltonNumber(in uint index, in uint base)
{
	float f = 1.0f;
	float result = 0.0f;

	for (uint i = index; i > 0;)
	{
		f /= base;
		result = result + f * (i % base);
		i = uint(i / float(base));
	}

	return result;
}

float2 CalculateHaltonSequence(in uint index)
{
	return float2(
		CalculateHaltonNumber((index & 0xFFu) + 1, 2),
		CalculateHaltonNumber((index & 0xFFu) + 1, 3));
}

#endif  // HALTON_HLSLI
