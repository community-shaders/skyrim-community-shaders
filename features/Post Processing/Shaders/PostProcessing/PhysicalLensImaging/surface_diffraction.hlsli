// Unresolved equivalent-pupil defects. Analytic Fourier intensity of curved
// Gaussian grooves and soft particles, summed incoherently. This is a
// weak-scattering approximation, not a measured dirty front element or a full
// propagation through every internal surface. All work runs at cache rebuild.
// Fourier aperture principle: Goodman, Introduction to Fourier Optics; see also
// https://wandell.github.io/FISE-git/chapters/optics-03-diffraction-psf.html
// The random dimensions/energy budget are synthetic assumptions, not measurements.
float SurfaceRandom(uint seed)
{
	seed ^= seed >> 16;
	seed *= 0x7feb352du;
	seed ^= seed >> 15;
	seed *= 0x846ca68bu;
	seed ^= seed >> 16;
	return float(seed & 0xffffffu) / 16777216.;
}
float CurvedGrooveSpectrum(float2 frequency, float2 axis, float lengthMM, float widthMM, float curvature)
{
	// Fourier transform of exp(-t^2/(2 L^2)) along
	// r(t) = axis*t + normal*curvature*t^2, with Gaussian width W.
	// The quadratic phase integral is analytic: curved grooves fan out instead
	// of becoming ideal straight sinc stripes. No hand-authored radial rays.
	float tangent = dot(frequency, axis), normal = dot(frequency, float2(-axis.y, axis.x));
	float chirp = 2 * curvature * normal * lengthMM * lengthMM;
	float variance = 1 + chirp * chirp;
	return lengthMM * widthMM * rsqrt(variance) * exp(-pow(tangent * lengthMM, 2) / variance - pow(normal * widthMM, 2));
}
float SurfaceDensity(float2 position, uint band)
{
	float wavelength = band == 0 ? .65 : band == 1 ? .55 :
	                                                 .45;
	float radiusMM = max(WaveSurface.z, .1);
	float2 k = PI * WavePupilSize / 512 * position / radiusMM;
	float density = 0;
	uint seed = uint(WaveSurface.y) * 131;
	// The shared spectrum filter integrates wavelengths after this cache pass.
	[unroll] for (uint spectral = 0; spectral < 1; ++spectral)
	{
		float lambda = wavelength;
		float scale = wavelength / lambda;
		float2 frequency = k * scale;
		float sum = 0;
		[loop] for (uint scratch = 0; scratch < 12; ++scratch)
		{
			uint key = seed + scratch * 17;
			// Several grooves share a wiping direction, with independent lengths,
			// widths and curvatures. A small isotropic population remains.
			float angle = scratch < 8 ? 2 * PI * SurfaceRandom(seed + 733) + .7 * (SurfaceRandom(key + 1) - .5) : 2 * PI * SurfaceRandom(key + 1);
			float2 axis = float2(cos(angle), sin(angle));
			// RMS dimensions; most of each groove lies within +/- 2 sigma.
			float lengthMM = min(.10 + .65 * SurfaceRandom(key + 3), .45 * radiusMM);
			float widthMM = .001 + .003 * SurfaceRandom(key + 5);
			float curvature = .02 + .16 * SurfaceRandom(key + 7);
			sum += CurvedGrooveSpectrum(frequency, axis, lengthMM, widthMM, curvature);
		}
		// A distribution of soft particle sizes produces a broad low-energy halo.
		[unroll] for (uint particle = 0; particle < 4; ++particle)
		{
			float sigma = .008 + .032 * SurfaceRandom(seed + 311 + particle * 7);
			sum += .25 * 4 * PI * sigma * sigma * exp(-sigma * sigma * dot(frequency, frequency));
		}
		density += sum * scale * scale;
	}
	return density / (radiusMM * radiusMM);
}
float2 SurfaceErf(float2 x)
{
	float2 t = rcp(1 + .3275911 * abs(x));
	float2 p = (((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - .284496736) * t + .254829592;
	return sign(x) * (1 - p * t * exp(-x * x));
}
float PhaseGratingMass(float2 lo, float2 hi)
{
	// First Born term of a small sinusoidal phase film in a Gaussian window:
	// its Fourier amplitude has two displaced Gaussian orders. Unlike
	// independent scratches, correlated microridges produce separated spectra.
	// Exact cell integrals retain these narrow orders in the logarithmic atlas.
	float radius = max(WaveSurface.z, .1);
	float frequencyScale = PI * WavePupilSize / (512 * radius);
	uint seed = uint(WaveSurface.y) * 131;
	float mass = 0;
	[unroll] for (uint patch = 0; patch < 2; ++patch)
	{
		uint key = seed + 859 + patch * 19;
		float angle = 2 * PI * SurfaceRandom(key);
		float periodMM = .005 + .010 * SurfaceRandom(key + 1);
		float sigmaMM = .02 + .02 * SurfaceRandom(key + 2);
		float2 carrier = (2 * PI / periodMM) * float2(cos(angle), sin(angle));
		[unroll] for (uint order = 0; order < 2; ++order)
		{
			float2 center = order == 0 ? carrier : -carrier;
			float2 integral = SurfaceErf(sigmaMM * (hi * frequencyScale - center)) - SurfaceErf(sigmaMM * (lo * frequencyScale - center));
			mass += .08 * PI * max(integral.x, 0) * max(integral.y, 0) / (4 * frequencyScale * frequencyScale * radius * radius);
		}
	}
	return mass;
}
// Integrate over each logarithmic cache cell. Narrow streaks must not be
// represented by one point sample. Subdivision is cache-only and bounded.
float SurfaceMass(float2 lo, float2 hi, uint band)
{
	if (any(hi <= lo))
		return 0;
	float sum = 0;
	[unroll] for (uint y = 0; y < 2; ++y)
		[unroll] for (uint x = 0; x < 2; ++x)
			sum += SurfaceDensity(lerp(lo, hi, float2(x, y) * .577350269 + .211324865), band);
	return sum * .25 * (hi.x - lo.x) * (hi.y - lo.y) + PhaseGratingMass(lo, hi);
}
