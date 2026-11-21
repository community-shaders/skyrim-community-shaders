#ifndef PHYSICAL_LENS_RECONSTRUCTION
#define PHYSICAL_LENS_RECONSTRUCTION
float3 SamplePhysicalBands(Texture2DArray<float> tex, SamplerState samp, float2 uv, float mip)
{
	return float3(tex.SampleLevel(samp, float3(uv, 0), mip), tex.SampleLevel(samp, float3(uv, 1), mip), tex.SampleLevel(samp, float3(uv, 2), mip));
}
float3 SamplePhysicalMip(Texture2DArray<float> tex, SamplerState samp, float2 uv, float2 texSize, float mip)
{
	float2 samplePos = uv * texSize;
	float2 tc = floor(samplePos - 0.5) + 0.5;
	float2 f = samplePos - tc;
	float2 f2 = f * f;
	float2 f3 = f2 * f;

	// Catmull-Rom weights
	float2 w0 = f2 - 0.5 * (f3 + f);
	float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
	float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
	float2 w3 = 0.5 * (f3 - f2);

	// Collapse the center weights; retain the existing five-tap approximation.
	float2 w12 = w1 + w2;
	float2 tc0 = (tc - 1.0) / texSize;
	float2 tc12 = (tc + w2 / w12) / texSize;
	float2 tc3 = (tc + 2.0) / texSize;

	float3 result =
		SamplePhysicalBands(tex, samp, float2(tc12.x, tc0.y), mip) * (w12.x * w0.y) +
		SamplePhysicalBands(tex, samp, float2(tc0.x, tc12.y), mip) * (w0.x * w12.y) +
		SamplePhysicalBands(tex, samp, float2(tc12.x, tc12.y), mip) * (w12.x * w12.y) +
		SamplePhysicalBands(tex, samp, float2(tc3.x, tc12.y), mip) * (w3.x * w12.y) +
		SamplePhysicalBands(tex, samp, float2(tc12.x, tc3.y), mip) * (w12.x * w3.y);

	// The 4 corner taps have very small weights, skip for performance
	return result;
}
#endif
