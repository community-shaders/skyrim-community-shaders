// Hullin et al. (2011), section 3.3: scale Fourier spectra by wavelength
// before integrating the visible spectrum. Detailed pupils also include an
// empirical phase screen; its wavelength dependence uses three phase anchors.
#include "PostProcessing/PhysicalLensImaging/diffraction.cs.hlsl"

// CIE 2015 2-degree observer * D65, 390-730 nm at 5 nm spacing, converted
// to signed linear sRGB and normalized per channel. Observer/illuminant data:
// CIE tables also distributed by Realflare (Beat Reichenbach, GPL-3.0-or-later).
// Do not clamp individual wavelengths: that changes the color of diffraction.
static const float3 AmplitudeSpectrum[69] = {
	float3(5.811962984e-05, -5.145221025e-05, 0.0004875755815),
	float3(0.0001787421016, -0.0001592621539, 0.001529423836),
	float3(0.0004956468678, -0.0004548263459, 0.004380639534),
	float3(0.001092736998, -0.001042650313, 0.009970148901),
	float3(0.002112421117, -0.002084316854, 0.01992416007),
	float3(0.003271412387, -0.003414562634, 0.03295690687),
	float3(0.004368321601, -0.004822181294, 0.04746785361),
	float3(0.004676144957, -0.005548725795, 0.05675016892),
	float3(0.004689202218, -0.006041105491, 0.06497015259),
	float3(0.004739996284, -0.006934516073, 0.08076824203),
	float3(0.004482377268, -0.007653743178, 0.09693300371),
	float3(0.003140673924, -0.007127590221, 0.1025049226),
	float3(0.001254564775, -0.005973141531, 0.1040544668),
	float3(-0.001321844876, -0.00361874228, 0.09383687082),
	float3(-0.004463727813, -0.0009414104407, 0.08587271529),
	float3(-0.00813591503, 0.0022385671, 0.07926169032),
	float3(-0.0122533399, 0.006181867559, 0.06808031962),
	float3(-0.01634181688, 0.0105267539, 0.05406880672),
	float3(-0.02009928625, 0.01488383997, 0.04033613453),
	float3(-0.02261806293, 0.01837153169, 0.02827395692),
	float3(-0.02454996566, 0.02153445068, 0.01906625974),
	float3(-0.02791615498, 0.02634487576, 0.01339921014),
	float3(-0.03171453626, 0.0322798816, 0.008952063326),
	float3(-0.03565584002, 0.03908574215, 0.004671169666),
	float3(-0.03911998962, 0.04651697359, 0.0009339458281),
	float3(-0.04082419604, 0.05344599968, -0.001768273542),
	float3(-0.04111540979, 0.05953937497, -0.003916777053),
	float3(-0.03954739713, 0.06496771723, -0.005565447339),
	float3(-0.03583781604, 0.06894447152, -0.006798578285),
	float3(-0.02965496863, 0.0695842864, -0.007468175789),
	float3(-0.02194268309, 0.0695034681, -0.007918820774),
	float3(-0.01304917415, 0.0685434593, -0.008165174272),
	float3(-0.003074957525, 0.06597743735, -0.008160077841),
	float3(0.007949984135, 0.0621128802, -0.007969126903),
	float3(0.02041549001, 0.05695989961, -0.00761358144),
	float3(0.03310550392, 0.05162681542, -0.007221727194),
	float3(0.04695228803, 0.04532538898, -0.006719452792),
	float3(0.06124252307, 0.03903506521, -0.006220985157),
	float3(0.07505776087, 0.03196996486, -0.00561026806),
	float3(0.0863701979, 0.02439563083, -0.004881100116),
	float3(0.09351134901, 0.01769886069, -0.004176485574),
	float3(0.1021296959, 0.01224672895, -0.003659925951),
	float3(0.1077453247, 0.007326305728, -0.003148924385),
	float3(0.109394338, 0.003063525529, -0.002639892493),
	float3(0.1067647207, -0.0001239238894, -0.002179755832),
	float3(0.09996340482, -0.002292088365, -0.001763642307),
	float3(0.09105049409, -0.003628713053, -0.001410330517),
	float3(0.07989419116, -0.004255226174, -0.001100961506),
	float3(0.0667735397, -0.004164870582, -0.0008425808238),
	float3(0.05598422526, -0.003894115894, -0.0006551570373),
	float3(0.04638722439, -0.003515351034, -0.0005060305484),
	float3(0.03705044453, -0.003011922832, -0.0003781507729),
	float3(0.02830764986, -0.002383517952, -0.0002784234088),
	float3(0.02158530561, -0.001872010621, -0.0002053543368),
	float3(0.01623816927, -0.0014469347, -0.0001495547348),
	float3(0.01219088356, -0.001109430617, -0.0001093290561),
	float3(0.009020441938, -0.0008313188478, -7.956849068e-05),
	float3(0.006340020468, -0.0005894442896, -5.526794532e-05),
	float3(0.004388167266, -0.0004104661942, -3.793562446e-05),
	float3(0.002900063105, -0.0002723888559, -2.49283367e-05),
	float3(0.001877968265, -0.0001768222371, -1.608731742e-05),
	float3(0.001325665265, -0.0001250291485, -1.132937503e-05),
	float3(0.0009352536567, -8.823118079e-05, -7.989861301e-06),
	float3(0.000661232429, -6.23809802e-05, -5.648802682e-06),
	float3(0.0004616470174, -4.35166662e-05, -3.948281083e-06),
	float3(0.0002888683935, -2.717508598e-05, -2.477560018e-06),
	float3(0.0001807410996, -1.696717117e-05, -1.554755509e-06),
	float3(0.000133311612, -1.248236577e-05, -1.150884282e-06),
	float3(9.863352385e-05, -9.208997529e-06, -8.548670987e-07)
};
// Retained for the independent real-amplitude FFT reference and A/B harness.
[numthreads(8, 8, 1)] void BuildAmplitudePupil(uint3 id : SV_DispatchThreadID) {
	int2 p = int2(id.xy) - int((512 - WavePupilSize) / 2);
	float amplitude = 0;
	if (all(p >= 0) && all(p < int(WavePupilSize))) {
		[loop] for (uint sy = 0; sy < 8; ++sy)[loop] for (uint sx = 0; sx < 8; ++sx)
		{
			float2 q = (float2(p) + (float2(sx, sy) + .5) / 8 - .5 * WavePupilSize) * (2.4 / WavePupilSize);
			float clearance = 1 - length(q);
			if (Blades >= 3) {
				float sector = 2 * PI / Blades, angle = atan2(q.y, q.x) - Rotation;
				float delta = angle - sector * floor(angle / sector + .5);
				float2 local = length(q) * float2(cos(delta), sin(delta));
				float apothem = cos(PI / Blades), sag = (1 - apothem) * BladeCurvature;
				float edgeLength = 2 * sin(PI / Blades);
				clearance = min(clearance, apothem + sag - local.x - 4 * sag * local.y * local.y / (edgeLength * edgeLength));
				if (BladeCurvature < 0)
					clearance = CameraIrisClearance(q, Blades, Rotation, BladeCurvature);
			}
			amplitude += clearance >= 0 ? 1. / 64 : 0;
		}
		if (WaveSurface.x > 0) {
			float stepMM = 2.4 * WaveSurface.z / WavePupilSize;
			float2 mm = (float2(p) + .5 - .5 * WavePupilSize) * stepMM;
			// Real transmission only: the diffraction structure is generated
			// by the aperture, dust and scratches, not by added ray sprites.
			amplitude *= exp(-WaveSurface.x * Microstructure(mm, stepMM, uint(WaveSurface.y)).x);
		}
	}
	WavePupil[id] = float4(amplitude, 0, 1, amplitude * amplitude);
}

float DetailedCorner(float2 p, bool2 mirror, uint band)
{
	float2 c = min(p, 511.5) - .5;
	c = float2(mirror.x ? 1023 - c.x : c.x, mirror.y ? 1023 - c.y : c.y);
	int2 base = int2(floor(c));
	float2 f = frac(c);
	float a = WaveInput.Load(int4(clamp(base, 0, 1023), band, 0)).y;
	float b = WaveInput.Load(int4(clamp(base + int2(1, 0), 0, 1023), band, 0)).y;
	float d = WaveInput.Load(int4(clamp(base + int2(0, 1), 0, 1023), band, 0)).y;
	float e = WaveInput.Load(int4(clamp(base + 1, 0, 1023), band, 0)).y;
	return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y) * saturate(p.x + .5) * saturate(p.y + .5);
}
float IntegrateDetailedBand(float2 lo, float2 hi, uint band)
{
	if (any(hi <= lo))
		return 0;
	// Atlas coordinates retain the old 512-FFT physical units.
	lo = lo * 2 + 512;
	hi = hi * 2 + 512;
	float sum = 0;
	[unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
	{
		bool2 mirror = bool2(x != 0, y != 0);
		float2 a = float2(x ? 1023 - hi.x : lo.x, y ? 1023 - hi.y : lo.y);
		float2 b = float2(x ? 1023 - lo.x : hi.x, y ? 1023 - lo.y : hi.y);
		if (all(a < 511.5) && all(b > -.5))
			sum += DetailedCorner(b, mirror, band) - DetailedCorner(float2(a.x, b.y), mirror, band) - DetailedCorner(float2(b.x, a.y), mirror, band) + DetailedCorner(a, mirror, band);
	}
	return max(sum, 0);
}
float IntegrateDetailed(float2 lo, float2 hi)
{
	// A real transmission mask shares one power spectrum across wavelengths.
	return IntegrateDetailedBand(lo, hi, 0);
}

[numthreads(8, 8, 1)] void FilterAmplitudeSpectrum(uint3 id : SV_DispatchThreadID) {
	float2 lo = WaveDomain(float2(id.xy) - 256 - .5), hi = WaveDomain(float2(id.xy) - 256 + .5);
	float area = (hi.x - lo.x) * (hi.y - lo.y);

	float central = 0, tail = 0;
	float2 core = 1.5 / WaveSampling[id.z].xy;
	float2 pixel = (float2(id.xy) - 256) * .5;
	[loop] for (uint sample = 0; sample < 69; ++sample)
	{
		float wavelength = .390 + .005 * sample;
		float2 ratio = WaveSampling[id.z].xy / WaveSampling[1].xy * (.55 / wavelength);
		float weight = AmplitudeSpectrum[sample][id.z];
		float2 a = lo * ratio, b = hi * ratio;
		// Same power spectrum at every wavelength; integrate the cell rather
		// than point-sampling narrow spikes on the compressed tail grid.
		float support = 1 - smoothstep(160, 240, length(.5 * (a + b)));
		float2 innerLo = max(lo, -core), innerHi = min(hi, core);
		float inner = all(innerHi > innerLo) ? IntegrateDetailed(innerLo * ratio, innerHi * ratio) : 0;
		tail += weight * max(IntegrateDetailed(a, b) - inner, 0) * support;
		central += weight * IntegrateDetailed((pixel - .25) * ratio, (pixel + .25) * ratio);
	}
	// Keep signed RGB through spectral integration; clip only the final pixel.
	tail = max(tail, 0);

	DiffractionPSF[id] = float4(max(central, 0), tail, tail / max(area, 1e-20), 0);
}
