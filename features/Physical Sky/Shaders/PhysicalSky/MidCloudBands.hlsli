#ifndef PHYSICAL_SKY_MID_CLOUD_BANDS_HLSLI
#define PHYSICAL_SKY_MID_CLOUD_BANDS_HLSLI

// Integral of max(a + b*cos(x), 0). Phase is in cycles; the primitive is
// continuous across periods. Clip before filtering so unresolved clear/cloud
// bands retain condensate instead of cancelling back to a uniform deficit.
float MiddleBandPrimitive(float phase, float a, float b, float cutoff, float halfMass)
{
	const float period = floor(phase + 0.5);
	const float angle = clamp((phase - period) * 6.28318530718, -cutoff, cutoff);
	return period * (2.0 * halfMass) + a * angle + b * sin(angle);
}

float MiddleBandPositiveMean(float phase, float width, float a, float b, float wave)
{
	if (a >= b)
		return max(a + b * wave, 0.0);
	if (a <= -b)
		return 0.0;
	const float cutoff = acos(clamp(-a / b, -1.0, 1.0));
	const float halfMass = a * cutoff + b * sin(cutoff);
	const float lo = MiddleBandPrimitive(phase - 0.5 * width, a, b, cutoff, halfMass);
	const float hi = MiddleBandPrimitive(phase + 0.5 * width, a, b, cutoff, halfMass);
	return max((hi - lo) / (6.28318530718 * width), 0.0);
}

// Coverage sets the local condensation margin, not an exact duty cycle.
// Dry columns remain clear and saturated columns can merge across wave troughs.
// Intermediate moisture produces separated crests; width follows the weather.
// Intermediate organization interpolates signed surplus, not cloudy masks:
// increasing organization can open real gaps before reaching its endpoint.
float2 MiddleBandDistribution(float coverage, float organization, float phase, float width)
{
	phase = frac(phase + 0.5) - 0.5;
	const float carrier = cos(6.28318530718 * phase);
	const float a = lerp(coverage, 2.0 * coverage - 1.0, organization);
	const float amplitude = organization * (4.0 * coverage * (1.0 - coverage));
	float density = saturate(a + amplitude * carrier);
	float wave = carrier;
	[branch] if (width > 0.25)
	{
		// Filter as a footprint approaches the two-samples-per-period limit.
		// Resolved bands retain their world-space profile.
		const float filterWeight = smoothstep(0.25, 0.5, width);
		const float angle = 3.14159265359 * width;
		const float averageWave = carrier * sin(angle) / angle;
		float mean = MiddleBandPositiveMean(phase, width, a, amplitude, averageWave);
		if (a + amplitude > 1.0)
			mean -= MiddleBandPositiveMean(phase, width, a - 1.0, amplitude, averageWave);
		density = lerp(density, saturate(mean), filterWeight);
		wave = lerp(wave, averageWave, filterWeight);
	}
	return float2(density, wave);
}

#endif
