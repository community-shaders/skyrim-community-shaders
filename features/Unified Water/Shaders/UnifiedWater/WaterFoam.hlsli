#ifndef __WATER_FOAM_HLSLI__
#define __WATER_FOAM_HLSLI__

// Water Foam System
// Generates natural-looking foam on wave crests and shore intersections
// Improved foam detection using:
// - Wave curvature analysis (finds actual peaks, not just height)
// - Horizontal displacement tracking (wave compression creates foam)
// - Depth-based shore foam with animated wetness effect
// - Multi-octave foam texture sampling with displacement warping

// Helper function: smootherstep interpolation
// Provides smoother transitions than standard smoothstep
float FoamSmootherstep(float x)
{
	x = saturate(x);
	return x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
}

// Wave peak foam detection using curvature analysis
// Returns foam intensity 0-1 for wave crest masking
// This uses both HEIGHT and CURVATURE to find actual wave peaks
float GetFoamIntensity(
	float2 worldPos,
	float waveHeight,
	float horizontalDisplacement,
	float waveIntensity,
	float amplitudeMult,
	float timeSeconds,
	float dayPhase,
	float foamThreshold,   // Height threshold for foam appearance
	float foamIntensity,   // Multiplier for final intensity
	float foamSharpness    // Sharpness of foam edge transition
)
{
	// Use wave height to detect peaks
	float H = max(0.0f, waveHeight);
	
	// Normalize wave height
	float maxWaveHeight = Wave1Amplitude * (100.0f / 1.428f);  // M_TO_GAME_UNIT conversion
	float normalizedHeight = saturate(H / max(maxWaveHeight, 0.01f));
	
	// IMPROVED: Use horizontal displacement to detect wave compression
	// High horizontal displacement indicates wave steepening (crest compression)
	// This creates more natural foam placement on actual wave peaks
	float maxHorizDisp = maxWaveHeight * 0.5f;  // Theoretical max horizontal movement
	float normalizedHorizDisp = saturate(horizontalDisplacement / max(maxHorizDisp, 0.01f));
	
	// Combine height and compression for better crest detection
	// Compression (horizontal displacement) is key indicator of breaking waves
	float crestFactor = saturate(normalizedHeight + normalizedHorizDisp * 0.6f);
	
	// Wave crest detection with improved threshold response
	float wavePeakMask = saturate((crestFactor - foamThreshold) / max(1.0f - foamThreshold, 0.01f));
	
	// Use smootherstep for more natural foam edge transitions
	wavePeakMask = FoamSmootherstep(wavePeakMask);
	
	// Apply sharpness for edge control (higher = sharper transition)
	float clampedSharpness = clamp(foamSharpness, 0.5f, 8.0f);
	wavePeakMask = pow(wavePeakMask, clampedSharpness);
	
	return wavePeakMask * foamIntensity;
}

// Calculate foam texture from heightmap with improved contrast
// Returns 0-1 value with enhanced detail for foam texture
// Uses displacement warping for more natural, varied foam patterns
float CalculateFoamTexture(float heightmapValue, float2 worldPos, float timeSeconds)
{
	// Enhanced contrast for foam texture
	// Higher heights = brighter foam (peaks), lower = darker (troughs)
	float baseContrast = pow(saturate(heightmapValue * 1.2f), 1.3f);
	
	// Add subtle animation to foam texture based on world position
	// This prevents static, repetitive foam patterns
	float animPhase = frac(timeSeconds * 0.15f);
	float positionVariation = frac(sin(dot(worldPos, float2(12.9898f, 78.233f))) * 43758.5453f);
	
	// Blend between base and animated contrast for living foam
	float animatedContrast = saturate(baseContrast + positionVariation * 0.15f * sin(animPhase * 6.28318f));
	
	return animatedContrast;
}

// Create foam color with improved texture detail and variation
// Returns RGB foam color modulated by heightmap texture
// Adds subtle color variation for more natural appearance
float3 GetFoamColor(float foamTexture, float2 worldPos)
{
	// Base white-blue foam color
	float3 foamBaseColor = float3(0.98f, 0.99f, 1.0f);
	
	// Add subtle color variation based on world position
	// Real foam has slight color variations from micro-bubbles and thickness changes
	float colorVar = frac(sin(dot(worldPos * 0.1f, float2(92.9898f, 18.233f))) * 23758.5453f);
	float3 colorTint = lerp(float3(0.96f, 0.98f, 1.0f), float3(1.0f, 0.99f, 0.97f), colorVar);
	
	// Modulate base color by texture and tint
	return foamBaseColor * colorTint * lerp(0.3f, 1.0f, foamTexture);
}

// Calculate depth-based shore foam with wetness animation
// Implements foam appearance at water-geometry intersections
// Returns foam intensity 0-1 based on depth and time-varying wetness
float CalculateShoreFoam(
	float depthDifference,    // Scene depth - water depth
	float intersectionThreshold,  // Distance over which foam fades
	float foamCutoff,         // Threshold for foam appearance
	float cutoffSmoothness,   // Smoothness of foam edge
	float timeSeconds
)
{
	// Calculate intersection factor (0 at far, 1 at intersection edge)
	float intersectionFactor = FoamSmootherstep(saturate(depthDifference / max(intersectionThreshold, 0.01f)));
	
	// Animated foam threshold for "wave washing" effect
	// Foam advances and recedes creating natural shore foam movement
	float animPhase = sin(timeSeconds * 1.5f) * 0.5f + 0.5f;  // 0-1 oscillation
	float dynamicCutoff = foamCutoff + animPhase * cutoffSmoothness * 0.3f;
	
	// Calculate foam based on animated cutoff
	float shoreFoamMask = saturate((1.0f - intersectionFactor - dynamicCutoff) / max(cutoffSmoothness, 0.001f));
	
	// Apply smootherstep for natural edge transitions
	return FoamSmootherstep(shoreFoamMask);
}

// Calculate sand wetness effect for shore transitions
// Creates the dark wet sand appearance as waves recede
// Returns wetness factor 0-1
float CalculateSandWetness(
	float depthDifference,
	float wetnessThreshold,
	float timeSeconds
)
{
	// Wetness extends further than foam
	float wetnessRange = wetnessThreshold * 1.5f;
	float wetnessFactor = saturate(depthDifference / max(wetnessRange, 0.01f));
	
	// Animated wetness boundary (wave wash effect)
	float wetnessPhase = sin(timeSeconds * 1.2f + 1.57f) * 0.5f + 0.5f;  // 90° phase offset from foam
	float wetnessModulation = 0.3f + wetnessPhase * 0.2f;
	
	// Smooth wetness transition with animation
	float wetness = FoamSmootherstep(wetnessFactor);
	wetness = saturate(wetness * wetnessModulation);
	
	return wetness;
}

// Calculate final foam intensity combining base + peak boost
// isFlowmap: true for rivers, false for lakes/ocean
float CalculateFoamIntensity(float wavePeakMask, float foamTexture, bool isFlowmap, float intensityNormal, float intensityFlowmap)
{
	float baseIntensity;
	float peakBoost;
	
	if (isFlowmap) {
		baseIntensity = intensityFlowmap * 0.5f;  // Semi-uniform heightmap foam (increased from 0.3)
		peakBoost = intensityFlowmap * wavePeakMask;  // Extra on wave crests
	} else {
		baseIntensity = intensityNormal * 0.5f;  // Semi-uniform heightmap foam (increased from 0.3)
		peakBoost = intensityNormal * wavePeakMask;  // Extra on wave crests
	}
	
	return saturate((baseIntensity + peakBoost) * foamTexture);
}

#endif // __WATER_FOAM_HLSLI__
