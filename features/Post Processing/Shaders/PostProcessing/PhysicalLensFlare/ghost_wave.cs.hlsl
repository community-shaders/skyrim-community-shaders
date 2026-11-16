#include "PostProcessing/PhysicalLensImaging/aperture.hlsli"
#include "PostProcessing/PhysicalLensImaging/microstructure.hlsli"
#include "PostProcessing/PhysicalLensImaging/spectrum.hlsli"

// Hullin et al. eq. (5). Cache a finite-bandwidth 2D fractional Fourier
// aperture; never evaluate this transform per ghost or per frame.
cbuffer GhostWaveParameters : register(b1)
{
	float4 Aperture;  // blades, curvature, rotation, f-number
	float4 Surface;   // amount, entrance pupil radius mm, stable seed, unused
	uint4 Stage;      // wavelength sample, inverse FFT, Fresnel multiplier, unused
};
Texture2DArray<float4> Field : register(t0);
Texture2D<float4> Material : register(t1);
RWTexture2DArray<float4> OutField : register(u0);
RWTexture2D<float4> Atlas : register(u1);
RWTexture2D<float4> OutMaterial : register(u2);
static const float Pi = 3.141592653589793;
#ifndef PHYSICAL_LENS_GHOST_WAVE_SIZE
#	define PHYSICAL_LENS_GHOST_WAVE_SIZE 512
#endif
static const uint Size = PHYSICAL_LENS_GHOST_WAVE_SIZE;
float2 ComplexMultiply(float2 a, float2 b) { return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
float2 Phasor(float phase)
{
	float s, c;
	sincos(phase, s, c);
	return float2(c, s);
}
float Theta(uint band)
{
	float wavelength = .4 + .01 * (Stage.x * 3 + band);
	return .15 * (wavelength / .4) * (Aperture.w / 18) * Pi * .5;
}
float Clearance(float2 p)
{
	float sector = 2 * Pi / Aperture.x, angle = atan2(p.y, p.x) - Aperture.z;
	float delta = angle - sector * floor(angle / sector + .5);
	float2 q = length(p) * float2(cos(delta), sin(delta));
	float apothem = cos(Pi / Aperture.x), sag = (1 - apothem) * Aperture.y;
	float edge = 2 * sin(Pi / Aperture.x);
	float clearance = 1 - (q.x + 4 * sag * q.y * q.y / (edge * edge)) / (apothem + sag);
	if (Aperture.y < 0)
		clearance = CameraIrisClearance(p, Aperture.x, Aperture.z, Aperture.y);
	return clearance;
}
[numthreads(8, 8, 1)] void BuildMaterial(uint3 id : SV_DispatchThreadID) {
	float coverage = 0;
	[unroll] for (uint y = 0; y < 4; ++y)
		[unroll] for (uint x = 0; x < 4; ++x)
			coverage += Clearance((id.xy + (float2(x, y) + .5) / 4 - Size * .5) * 4 / Size) >= 0;
	float2 micro = 0;
	if (Surface.x > 0 && coverage > 0)
		micro = Microstructure((id.xy + .5 - Size * .5) * 4 * Surface.y / Size, 4 * Surface.y / Size, (uint)Surface.z);
	OutMaterial[id.xy] = float4(coverage / 16, micro, 0);
}
	[numthreads(8, 8, 1)] void BuildField(uint3 id : SV_DispatchThreadID)
{
	float4 material = Material.Load(int3(id.xy, 0));
	float wavelength = .4 + .01 * (Stage.x * 3 + id.z);
	float2 p = float2(id.xy) - Size * .5;
	float phase = -Pi * tan(Theta(id.z) * .5) * 512 / (Size * Size) * dot(p, p) + 2 * Pi * Surface.x * material.z / wavelength;
	OutField[id] = float4(material.x * exp(-Surface.x * material.y) * Phasor(phase), 0, 0);
}
groupshared float2 Samples[Size];
[numthreads(256, 1, 1)] void Transform(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
	[unroll] for (uint half = 0; half < Size / 256; ++half)
	{
		uint x = lane + half * 256;
		float2 value = Field.Load(int4(x, group.x, group.y, 0)).xy;
		if (Stage.z) {
			float2 uv = float2(x, group.x);
			uv = uv >= Size * .5 ? uv - Size : uv;
			value = ComplexMultiply(value, Phasor(-Pi * sin(Theta(group.y)) / 512 * dot(uv, uv)));
		}
		Samples[reversebits(x) >> (32u - (uint)firstbithigh(Size))] = value;
	}
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint length = 2; length <= Size; length *= 2)
	{
		[unroll] for (uint block = 0; block < Size / 512; ++block)
		{
			uint butterfly = lane + block * 256;
			uint offset = butterfly % (length / 2), index = butterfly / (length / 2) * length + offset;
			float2 a = Samples[index], b = ComplexMultiply(Samples[index + length / 2], Phasor((Stage.y ? 2 : -2) * Pi * offset / length));
			// Each butterfly owns its pair; synchronize between stages.
			Samples[index] = a + b;
			Samples[index + length / 2] = a - b;
		}
		GroupMemoryBarrierWithGroupSync();
	}
	[unroll] for (uint part = 0; part < Size / 256; ++part)
	{
		uint x = lane + part * 256;
		OutField[uint3(group.x, x, group.y)] = float4(Samples[x] * (Stage.y ? 1. / Size : 1.), 0, 0);
	}
}
	[numthreads(8, 8, 1)] void Accumulate(uint3 id : SV_DispatchThreadID)
{
	float4 value = 0;
	[unroll] for (uint band = 0; band < 3; ++band)
	{
		float2 amplitude = Field.Load(int4(id.xy, band, 0)).xy;
		uint sample = Stage.x * 3 + band;
		if (sample < 31)
			value.rgb += dot(amplitude, amplitude) * LensSpectrumWeights[sample];
	}
	Atlas[id.xy] = (Stage.x ? Atlas[id.xy] : 0) + value;
}
