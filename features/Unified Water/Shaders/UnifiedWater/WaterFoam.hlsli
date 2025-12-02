#ifndef __WATER_FOAM_HLSLI__
#define __WATER_FOAM_HLSLI__

// Water Foam System
// Generates foam on wave crests based on wave height
// Foam consists of:
// - Base heightmap texture (provides parallax depth detail everywhere)
// - Peak boost (extra intensity on top 30% of wave crests, expanded from 20%)

// Wave peak foam detection
// Returns foam intensity 0-1 for wave crest masking
// foamThreshold: lower values = more foam coverage (0.3 = top 70% of waves, 0.9 = only sharp peaks)
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
	// Use wave height to detect peaks (same approach as wave edge highlights)
	float H = max(0.0f, waveHeight);
	
	// Normalize wave height
	// Wave1Amplitude is max wave height (defined in GerstnerWaves.hlsli)
	float maxWaveHeight = Wave1Amplitude * (100.0f / 1.428f);  // M_TO_GAME_UNIT conversion
	float normalizedHeight = saturate(H / max(maxWaveHeight, 0.01f));
	
	// Wave crest detection based on threshold
	// Lower threshold = more foam coverage, higher = only sharp peaks
	float wavePeakMask = saturate((normalizedHeight - foamThreshold) / max(1.0f - foamThreshold, 0.01f));
	
	// Apply sharpness for edge control (higher = sharper transition)
	// Clamp sharpness to reasonable range to prevent visual artifacts
	float clampedSharpness = clamp(foamSharpness, 0.5f, 8.0f);
	wavePeakMask = pow(wavePeakMask, clampedSharpness);
	
	return wavePeakMask * foamIntensity;
}

// Calculate foam texture from heightmap
// Returns 0-1 value with enhanced contrast for foam detail
float CalculateFoamTexture(float heightmapValue)
{
	// Enhance height contrast for foam texture
	// Higher heights = brighter foam (peaks), lower = darker (troughs)
	return pow(saturate(heightmapValue * 1.2f), 1.3f);
}

// Create foam color with texture detail
// Returns RGB foam color modulated by heightmap texture
float3 GetFoamColor(float foamTexture)
{
	// Base white-blue foam color modulated by height texture
	float3 foamBaseColor = float3(0.98f, 0.99f, 1.0f);
	return foamBaseColor * lerp(0.4f, 1.0f, foamTexture);
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
