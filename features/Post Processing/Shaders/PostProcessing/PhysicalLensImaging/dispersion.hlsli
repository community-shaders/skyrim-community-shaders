#ifndef PHYSICAL_LENS_DISPERSION
#define PHYSICAL_LENS_DISPERSION
// Catalogue-fit curvature constrained to preserve the prescription's nd and Vd.
float Index(float nd, float b, float wavelength, float2 curvature)
{
#ifdef PHYSICAL_LENS_CAUCHY_REFERENCE
	// Offline timing/reference only; production always uses catalogue curvature.
	return nd + b * (rcp(wavelength * wavelength) - rcp(.58756 * .58756));
#else
	const float qd = 1 / (.58756 * .58756), qf = 1 / (.48613 * .48613), qc = 1 / (.65627 * .65627);
	float q = rcp(wavelength * wavelength);
	return nd + (q - qd) * (b + curvature.x * (q + qd - qf - qc) + curvature.y * (q * q + q * qd + qd * qd - qf * qf - qf * qc - qc * qc));
#endif
}
#endif
