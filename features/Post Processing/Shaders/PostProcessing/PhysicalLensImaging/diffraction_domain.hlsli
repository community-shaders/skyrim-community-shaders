#ifndef PHYSICAL_LENS_DIFFRACTION_DOMAIN
#define PHYSICAL_LENS_DIFFRACTION_DOMAIN
#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"
// Keep the FFT core on its original grid; integrate progressively wider cells
// in the distant wings. The mapping and its first derivative are continuous.
// Detailed pupils use twice the atlas density: their dealiased FFT support
// ends at 240, so the old atlas's thousands of unused wing units are wasted.
float2 WaveDomain(float2 p)
{
	p *= WaveStarburst.w > 1 ? .5 : 1;
	float2 a = abs(p);
	return sign(p) * (min(a, 64) + 34.6246809813 * (exp2(max(a - 64, 0) / 24) - 1));
}
float2 WaveDomainInverse(float2 p)
{
	float2 a = abs(p);
	return sign(p) * (min(a, 64) + 24 * log2(1 + max(a - 64, 0) / 34.6246809813)) * (WaveStarburst.w > 1 ? 2 : 1);
}
#endif
