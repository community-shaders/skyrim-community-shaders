#ifndef PHYSICAL_SKY_CLOUD_NOISE_HLSLI
#define PHYSICAL_SKY_CLOUD_NOISE_HLSLI

float CloudDensityExponent(float height)
{
	return saturate((height - 0.25) * 4.0) * 0.29999998 + 0.35;
}

float2 ReconstructNdfVoxelNoiseComponents(float4 noise, float profile)
{
	// Nubis Cubed (2023), pp. 94, 99, 104: RG are low/high-frequency
	// Curly-Alligator wisps; BA are low/high-frequency Alligator billows.
	// These are value-erosion thresholds, not the NDF texture's density lobes.
	const float dimensionalProfile = saturate(profile);
	const float wispyThreshold = lerp(noise.r, noise.g, dimensionalProfile);
	const float billowyThreshold = lerp(noise.b, noise.a, pow(dimensionalProfile, 0.25)) * 0.3;
	const float billow = 1.0 - billowyThreshold;
	const float wisps = 1.0 - wispyThreshold;
	return float2(billow, wisps);
}

float ReconstructNdfVoxelNoiseDensity(float4 noise, float profile, float topType, float height)
{
	const float2 components = ReconstructNdfVoxelNoiseComponents(noise, profile);
	const float billow = components.x, wisps = components.y;
	// Keep the NDF type/height blend, subtractive profile, and downstream base
	// density shaping. NVDF's density-scale and camera fades do not apply here.
	const float difference = wisps - billow;
	const float typeBlend = difference * 0.5 * saturate((topType - 0.5) * 9.999998);
	const float composite = (billow + typeBlend + (difference * 0.7 - typeBlend) * saturate((height - 0.1) * 4.9999995)) * 0.975;
	return saturate(composite - (1.0 - min(0.7, profile)));
}

float ReconstructCloudBillow(float4 noise, float profile, float topType, float height)
{
	const float erosion = pow(noise.a, 0.45) * -0.3;
	const float smallErosion = noise.a * 0.2;
	const float heightNoise = pow(noise.r, 0.2) - smallErosion *
	                                                  (1.0 - pow(saturate((saturate((topType - 0.2) * 9.999999) * 5.0 + 5.0) * height), 3.0));
	const float rounded = saturate((pow(noise.r, 0.45) - erosion) / (1.0 - erosion));
	const float billow = sqrt(saturate((saturate(lerp(heightNoise, rounded, saturate((profile - 0.1) * 4.9999995))) - 0.4) * 1.6666666));
	return billow;
}

float ReconstructCloudWisp(float4 noise, float profile, float height, float distanceMeters, float storm)
{
	const float stormShape = lerp(noise.b, noise.g, pow(abs(storm), 0.1));
	const float shape = lerp(stormShape, noise.g, saturate((distanceMeters - 1000.0) * 0.001));
	return pow(lerp(noise.a, lerp(shape, noise.g, saturate((profile - 0.2) * 5.0)),
				   saturate((profile + 0.6) * 1.111111)),
		0.1 - saturate((height - 0.45) * 9.999998) * 0.05 * saturate((storm - 0.1) * 10.0));
}

float ReconstructCloudNoiseDensity(float4 noise, float profile, float topType, float height, float distanceMeters, float storm = 0.0, bool risingBillows = false)
{
	const float billow = ReconstructCloudBillow(noise, profile, topType, height);
	// An eruption keeps the rounded cumulus component along the whole column.
	// The storm anvil's high-altitude wisps otherwise fill the gaps between lobes.
	if (risingBillows) {
		// Keep the packed high-frequency Alligator field as subtractive surface
		// erosion. The rounded term alone mostly fills these small-scale valleys.
		const float detailErosion = .25 * (1.0 - sqrt(noise.a));
		return saturate(billow * 0.975 - (1.0 - min(0.95, profile)) - detailErosion);
	}
	const float wisps = ReconstructCloudWisp(noise, profile, height, distanceMeters, storm);
	const float difference = wisps - billow;
	const float typeBlend = difference * 0.5 * saturate((topType - 0.5) * 9.999998);
	const float composite = (billow + typeBlend + (difference * 0.7 - typeBlend) * saturate((height - 0.1) * 4.9999995)) * 0.975;
	return saturate(composite - (1.0 - min(0.7, profile)));
}

float ShapeCloudBaseDensity(float eroded, float height, float bottomWidth, float bottomPower, float shapingStart, float storm, out float lightDensity, bool risingBillows = false)
{
	lightDensity = 0.0;
	if (eroded <= 0.0)
		return 0.0;
	const float bottomBoost = pow(1.0 - eroded, 10.0) * 8.0;
	const float baseWidth = 10.0 - saturate((bottomWidth - 1.0) * 0.11111111) * 9.0;
	const float profileHeight = saturate((height - shapingStart) / max(1.0 - shapingStart, 1e-6));
	const float stormWidth = lerp(baseWidth, storm * 9.0 + 1.0, pow(abs(storm), 0.3));
	const float stormPower = lerp(bottomPower, 0.3, storm * storm);
	const float baseProfile = pow(profileHeight, 0.3) * pow(saturate(profileHeight * stormWidth), stormPower);
	const float density = lerp(bottomBoost, 1.0, saturate(height * 5.0)) * eroded * baseProfile;
	const float shapedDensity = pow(density, saturate((height - 0.25) * 4.0) * 0.29999998 + 0.35);
	// Lighting uses the density before the storm ceiling fade, enhanced by
	// height and the unsheared storm mask. View extinction uses the fade below.
	lightDensity = shapedDensity * (1.0 + 3.0 * pow(height, 4.0)) * (1.0 + 3.0 * pow(storm, 5.0));
	const float topFade = 1.0 - saturate((height - 0.8) * 5.0);
	// Volcanic caps already come from the displaced carrier; a global height
	// fade would slice protruding Alligator lobes off at the nominal ceiling.
	return risingBillows ? shapedDensity : shapedDensity * lerp(1.0, topFade * topFade, saturate(storm * 10.0));
}

#endif
