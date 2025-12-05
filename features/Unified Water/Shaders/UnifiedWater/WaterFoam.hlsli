#ifndef __WATER_FOAM_HLSLI__
#define __WATER_FOAM_HLSLI__

// Water Foam System
// Phase-based foam detection for wave crests
// Detects local wave peaks using normal curvature, not absolute height

float FoamSmootherstep(float x)
{
	x = saturate(x);
	return x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
}

// Phase-based foam detection using wave normal curvature
// Foam appears at local wave peaks (where normal is most vertical)
// This works for waves of any size based on their local geometry
float GetFoamIntensity(
	float2 worldPos,
	float waveHeight,
	float horizontalDisplacement,
	float waveIntensity,
	float amplitudeMult,
	float timeSeconds,
	float dayPhase,
	float foamThreshold,
	float foamIntensity,
	float foamSharpness
)
{
	// The key insight: foam appears where waves are at their LOCAL peak
	// This is best detected by the wave normal's vertical component (Z)
	// At wave peaks, normal points upward (Z close to 1)
	// On slopes, normal tilts (Z decreases)
	
	// We'll use horizontal displacement as a proxy for wave steepness
	// and combine it with height to detect local peaks
	
	// Normalize height to estimate wave scale
	float maxWaveHeight = Wave1Amplitude * (100.0f / 1.428f);  // M_TO_GAME_UNIT conversion
	float normalizedHeight = saturate(abs(waveHeight) / max(maxWaveHeight, 0.01f));
	
	// Calculate wave steepness from horizontal displacement
	// Steeper waves (high horiz displacement) are more likely to foam
	float steepness = 0.0f;
	if (abs(waveHeight) > 0.001f) {
		steepness = horizontalDisplacement / max(abs(waveHeight), 0.001f);
	}
	steepness = saturate(steepness);
	
	// Determine if this is a large or small wave based on displacement magnitude
	// Small waves have smaller absolute displacement but can be very steep
	float largeWaveInfluence = saturate(normalizedHeight * 2.0f);
	
	// Large waves need significant steepness to foam (avoid gentle rolling tops)
	float largeSlopeRequirement = FoamLargeWaveSlopeRequirement;
	float largeWaveFoam = saturate((steepness - largeSlopeRequirement) / max(0.3f, 0.01f));
	
	// Small waves foam much more easily - they're naturally sharper
	// Add a base offset so even mild steepness creates foam
	float smallWaveFoam = saturate(steepness * FoamSmallWaveSlopeMultiplier + FoamSmallWaveBaseOffset);
	
	// Blend based on wave scale
	float steepnessFactor = lerp(smallWaveFoam, largeWaveFoam, largeWaveInfluence);
	
	// For local peak detection: positive height indicates we're on the crest side
	// Negative height indicates trough - no foam there
	float crestMask = saturate(waveHeight * 10.0f);  // Positive displacement = crest
	
	// Use foamThreshold to control where foam appears on the wave
	// Lower threshold = more foam coverage down the sides
	// Higher threshold = only at the very peak
	float heightThreshold = lerp(foamThreshold, foamThreshold + 0.2f, foamIntensity * 0.5f);
	
	// For small waves, be more lenient with height requirement
	float heightRange = lerp(FoamSmallWaveHeightRange, 1.0f, largeWaveInfluence);
	float peakMask = saturate((normalizedHeight - heightThreshold * 0.5f) / max(heightRange, 0.01f));
	
	// Combine all factors:
	// - Must be on crest (positive height)
	// - Must have sufficient steepness for wave type
	// - Must be near peak height
	float foamMask = crestMask * steepnessFactor * peakMask;
	
	// Apply smootherstep for natural transitions
	foamMask = FoamSmootherstep(foamMask);
	
	// Apply sharpness control
	float clampedSharpness = clamp(foamSharpness, 0.5f, 8.0f);
	foamMask = pow(foamMask, clampedSharpness);
	
	return foamMask * foamIntensity;
}

// Simple flat white foam color
float3 GetFoamColor()
{
	return float3(1.0f, 1.0f, 1.0f);
}

#endif // __WATER_FOAM_HLSLI__
