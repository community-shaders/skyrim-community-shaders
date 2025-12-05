#ifndef __WATER_FOAM_HLSLI__
#define __WATER_FOAM_HLSLI__

// Water Foam System
// Physically-based foam detection for breaking waves
// Detects wave breaking based on wave steepness and curvature
// - Wave breaks when horizontal displacement exceeds critical threshold
// - Creates foam only at sharp, breaking wave crests (not smooth swells)

float FoamSmootherstep(float x)
{
	x = saturate(x);
	return x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
}

// Physically-based wave breaking detection
// Waves break when the horizontal displacement becomes excessive relative to vertical displacement
// This creates the characteristic "eclipsing" or "overturning" motion of breaking waves
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
	// Wave breaking criterion: horizontal displacement vs vertical displacement ratio
	// When horizontal motion exceeds vertical, wave is "breaking" (steepening/eclipsing)
	float verticalDisp = abs(waveHeight);
	float horizDisp = horizontalDisplacement;
	
	// Breaking occurs when horizontal/vertical ratio exceeds critical value
	// Higher ratio = sharper, more breaking wave = more foam
	float breakingRatio = 0.0f;
	if (verticalDisp > 0.001f) {
		breakingRatio = horizDisp / max(verticalDisp, 0.001f);
	}
	
	// Physical wave breaking threshold (typical value ~0.7-1.0)
	// Below this ratio: smooth rolling swell (no foam)
	// Above this ratio: wave is breaking/overturning (foam appears)
	float breakingThreshold = lerp(0.5f, 1.2f, foamThreshold);
	
	// Only apply foam where wave is actually breaking
	float breakingFactor = saturate((breakingRatio - breakingThreshold) / max(0.3f, 0.01f));
	
	// Additional requirement: wave must be elevated (positive height)
	// Breaking waves occur at crests, not troughs
	float crestMask = saturate(waveHeight * 2.0f);
	
	// Combine breaking detection with crest requirement
	float foamMask = breakingFactor * crestMask;
	
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
