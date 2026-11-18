#ifndef __PGRASS_MATERIAL_HLSLI__
#define __PGRASS_MATERIAL_HLSLI__

float3 GetStableClumpColor(GrassType bladeType, uint seed)
{
	float colorRandom = (float(seed) + 0.5f) * (1.0f / 256.0f);
	float valueRandom = (float((seed * 73u + 41u) & 0xFFu) + 0.5f) * (1.0f / 256.0f);

	float3 tint = lerp(bladeType.grassColorCool.rgb, bladeType.grassColorWarm.rgb, colorRandom);
	float value = 1.0f + (valueRandom * 2.0f - 1.0f) * bladeType.grassColorVar.y * 0.75f;

	return lerp(1.0f, tint * value, bladeType.clumpColorStrength);
}

float2 GetDistantRoughness(GrassType bladeType, float2 appearanceT)
{
	return mad(mad(bladeType.midRoughnessPolynomial.x, appearanceT, bladeType.midRoughnessPolynomial.y), appearanceT * appearanceT, bladeType.midRoughnessPolynomial.z);
}

float GetDistantRoughness(GrassType bladeType, float appearanceT)
{
	return GetDistantRoughness(bladeType, float2(appearanceT, appearanceT)).x;
}

float3 GetStabilizedBladeColor(GrassType bladeType, float3 clumpColor, float appearanceT, float baseShade)
{
	return lerp(bladeType.baseColor.rgb, bladeType.tipColor.rgb, appearanceT) * clumpColor * baseShade;
}

float3 GetStabilizedBladeColor(GrassType bladeType, float3 clumpColor, float appearanceT)
{
	float baseShade = lerp(1.0f - grassLightParams.w, 1.0f, smoothstep(0.0f, 0.5f, appearanceT));
	return GetStabilizedBladeColor(bladeType, clumpColor, appearanceT, baseShade);
}

void GetStabilizedBladeColor(GrassType bladeType, float3 clumpColor, float2 appearanceT, out float3 color0, out float3 color1)
{
	float2 baseShade = lerp(1.0f - grassLightParams.w, 1.0f, smoothstep(0.0f, 0.5f, appearanceT));

	color0 = GetStabilizedBladeColor(bladeType, clumpColor, appearanceT.x, baseShade.x);
	color1 = GetStabilizedBladeColor(bladeType, clumpColor, appearanceT.y, baseShade.y);
}

float3 GetDistantAOThicknessRoughness(GrassType bladeType, float appearanceT, float clumpDensity, float along)
{
	// Low and Far interpolate the same endpoint material that Mid reaches as its geometry becomes straight.
	float2 rungT = float2(0.25f, 0.5f);
	float2 rungRoughness = GetDistantRoughness(bladeType, rungT);
	float baseAO = bladeType.minAO * GetClumpAO(bladeType, clumpDensity, 0.0f);

	return float3(lerp(baseAO, 1.0f, along),
		lerp(bladeType.minMaxSubsurfaceOpacity.x, bladeType.minMaxSubsurfaceOpacity.y, appearanceT), lerp(rungRoughness.x, rungRoughness.y, along));
}

float3 GetDistantBladeColor(GrassType bladeType, float3 clumpColor, float along)
{
	// Fold the fixed endpoint samples so Low matches Far's vertex colours without evaluating the shade curve.
	float3 baseColor = lerp(bladeType.baseColor.rgb, bladeType.tipColor.rgb, 0.25f) * (1.0f - 0.5f * grassLightParams.w);
	float3 tipColor = lerp(bladeType.baseColor.rgb, bladeType.tipColor.rgb, 0.5f);

	return lerp(baseColor, tipColor, along) * clumpColor;
}

void GetGrassScatteringAlbedos(float3 baseColor, float3 scatteringColor, float opaqueFraction, float transmissionStrength,
	out float3 reflectionAlbedo, out float3 transmissionAlbedo)
{
	// OpenPBR thin subsurface: R = S*(1-g)/2, T = S*(1+g)/2; strength maps to g + 1.
	float subsurfaceWeight = 1.0f - saturate(opaqueFraction);
	float transmittedShare = saturate(0.5f * transmissionStrength);

	transmissionAlbedo = scatteringColor * (subsurfaceWeight * transmittedShare);
	reflectionAlbedo = baseColor * (1.0f - subsurfaceWeight) + scatteringColor * (subsurfaceWeight * (1.0f - transmittedShare));
}

#endif
