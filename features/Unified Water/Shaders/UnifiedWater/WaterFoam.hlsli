#ifndef __WATER_FOAM_HLSLI__
#define __WATER_FOAM_HLSLI__

// Water Foam System
// Generates foam on wave crests and steep wave faces based on wave height and displacement
// 
// Current Implementation:
// - Uses wave height (Z displacement) to detect crests
// - Uses horizontal displacement to detect steep slopes
// - More robust than Jacobian-based approach for cell-based statistical wave synthesis
//
// Legacy Jacobian functions are preserved below for reference but not currently used

// Foam calculation parameters
struct FoamParams
{
	float foamIntensity;      // Master foam strength (0-1)
	float foamThreshold;      // Jacobian threshold below which foam appears
	float foamSharpness;      // How sharp the foam edge is
	float foamPersistence;    // How long foam lingers (affects accumulation)
};

// Calculate the Jacobian determinant of Gerstner wave displacement
// Returns a value indicating surface compression:
// < 1.0 = compression (foam potential)
// = 1.0 = neutral
// > 1.0 = stretching
float CalculateGerstnerJacobian(
	float2 worldPos,
	float waveIntensity,
	float amplitudeMult,
	float steepnessMult,
	float timeSeconds,
	float dayPhase
)
{
	// The Jacobian for Gerstner waves can be computed analytically
	// For a single wave: J = 1 - Q*k*A*sin(phase)
	// For multiple waves, the contributions accumulate
	
	if (waveIntensity <= 0.001f) {
		return 1.0f;
	}
	
	float2 windDir = normalize(float2(0.707f, 0.707f));
	
	// Accumulate Jacobian contributions from each wave
	// Start with identity (J = 1)
	float jacobian = 1.0f;
	
	// Wave parameters (same as GerstnerWaves.hlsli)
	float wavelengthsM[6] = {
		max(Wave1Wavelength, 1.0f),
		max(Wave2Wavelength, 0.5f),
		max(Wave3Wavelength, 0.25f),
		max(Wave4Wavelength, 0.1f),
		max(Wave5Wavelength, 0.05f),
		max(Wave6Wavelength, 0.025f)
	};
	
	float amplitudesM[6] = {
		max(Wave1Amplitude, 0.0f),
		max(Wave2Amplitude, 0.0f),
		max(Wave3Amplitude, 0.0f),
		max(Wave4Amplitude, 0.0f),
		max(Wave5Amplitude, 0.0f),
		max(Wave6Amplitude, 0.0f)
	};
	
	float steepnesses[6] = {
		saturate(Wave1Steepness),
		saturate(Wave2Steepness),
		saturate(Wave3Steepness),
		saturate(Wave4Steepness),
		saturate(Wave5Steepness),
		saturate(Wave6Steepness)
	};
	
	float angleOffsets[6] = {
		Wave1AngleOffset,
		Wave2AngleOffset,
		Wave3AngleOffset,
		Wave4AngleOffset,
		Wave5AngleOffset,
		Wave6AngleOffset
	};
	
	float phaseOffsets[6] = {
		0.0f,
		dayPhase * 0.3f + 2.094f,
		dayPhase * 0.5f + 4.189f,
		dayPhase * 0.7f + 1.047f,
		dayPhase * 0.9f + 5.236f,
		dayPhase * 1.1f + 3.142f
	};
	
	[unroll]
	for (int i = 0; i < 6; ++i) {
		if (amplitudesM[i] < 0.0001f) {
			continue;
		}
		
		// Wave direction with offset
		float angle = angleOffsets[i];
		float cosA, sinA;
		sincos(angle, sinA, cosA);
		float2 waveDir = float2(
			windDir.x * cosA - windDir.y * sinA,
			windDir.x * sinA + windDir.y * cosA
		);
		
		// Convert to game units
		float wavelengthGame = wavelengthsM[i] * M_TO_GAME_UNIT;
		float amplitudeGame = amplitudesM[i] * waveIntensity * amplitudeMult * M_TO_GAME_UNIT;
		float steepness = steepnesses[i] * steepnessMult;
		
		// Wave number k = 2π / λ
		float k = UW_TWO_PI / wavelengthGame;
		
		// Angular frequency ω = √(g·k) with speed multiplier
		float gravityGame = UW_GRAVITY * M_TO_GAME_UNIT;
		float omega = sqrt(gravityGame * k) * WaveSpeed;
		
		// Phase at this position and time
		float phase = k * dot(waveDir, worldPos) - omega * timeSeconds + phaseOffsets[i];
		
		// Jacobian contribution: J -= Q*k*A*sin(phase)
		// Negative sin(phase) = wave crest = Jacobian decreases
		float sinPhase = sin(phase);
		jacobian -= steepness * k * amplitudeGame * sinPhase;
	}
	
	return jacobian;
}

// Calculate foam amount based on Jacobian and alpha height gradient
// Returns foam intensity 0-1
float CalculateFoamAmount(
	float jacobian,            // Gerstner wave Jacobian
	float alphaHeightGradient, // Gradient magnitude from alpha displacement (0 = flat, higher = steeper)
	FoamParams params
)
{
	// Foam from Gerstner Jacobian
	// Jacobian < threshold = foam region (surface compression/folding)
	float jacobianFoam = 0.0f;
	if (jacobian < params.foamThreshold) {
		// Map Jacobian to foam intensity
		// When J approaches 0 or negative, maximum foam
		float foamFactor = 1.0f - saturate(jacobian / params.foamThreshold);
		jacobianFoam = pow(foamFactor, params.foamSharpness);
	}
	
	// Foam from alpha tessellation displacement gradient
	// Steep gradients in the height field = wave peaks
	float alphaFoam = saturate(alphaHeightGradient * 2.0f);
	
	// Combine foam sources (additive with clamping)
	float totalFoam = saturate(jacobianFoam + alphaFoam * 0.5f);
	
	return totalFoam * params.foamIntensity;
}

// Sample foam for pixel shader
// worldPos: absolute world position
// waveHeight: current wave Z displacement from DS
// alphaDisplacement: alpha tessellation displacement value
// Returns foam color contribution (white foam, attenuated)
float3 SampleWaterFoam(
	float2 worldPos,
	float waveHeight,
	float alphaDisplacement,
	float waveIntensity,
	float amplitudeMult,
	float steepnessMult,
	float timeSeconds,
	float dayPhase,
	FoamParams params
)
{
	// Calculate Jacobian
	float jacobian = CalculateGerstnerJacobian(
		worldPos,
		waveIntensity,
		amplitudeMult,
		steepnessMult,
		timeSeconds,
		dayPhase
	);
	
	// Estimate alpha height gradient from displacement magnitude
	// Higher displacement = steeper surface = more foam potential
	float alphaGradient = abs(alphaDisplacement) * 0.1f;
	
	// Wave height also contributes - higher waves = more foam on crests
	// Positive wave height = crest
	float heightContribution = saturate(waveHeight * 0.02f);
	alphaGradient += heightContribution;
	
	// Calculate foam amount
	float foamAmount = CalculateFoamAmount(jacobian, alphaGradient, params);
	
	// Foam color - white with slight blue tint
	float3 foamColor = float3(0.95f, 0.97f, 1.0f);
	
	return foamColor * foamAmount;
}

// Wave peak foam detection - similar to wave edge highlight approach
// Returns foam intensity 0-1 based on wave crest height
// Only shows foam on the TOP 20% of wave crests
float GetFoamIntensity(
	float2 worldPos,
	float waveHeight,
	float horizontalDisplacement,
	float waveIntensity,
	float amplitudeMult,
	float timeSeconds,
	float dayPhase,
	float foamThreshold,   // Default: 0.8 (now 0.8 = top 20% threshold)
	float foamIntensity,   // Default: 1.0
	float foamSharpness    // Default: 2.0
)
{
	// Use wave height to detect peaks (same approach as wave edge highlights)
	float H = max(0.0f, waveHeight);
	
	// Normalize wave height
	// Wave1Amplitude is max wave height (defined above)
	float maxWaveHeight = Wave1Amplitude * (100.0f / 1.428f);  // M_TO_GAME_UNIT conversion
	float normalizedHeight = saturate(H / max(maxWaveHeight, 0.01f));
	
	// Only foam on top portion of wave peaks (threshold = 0.8 means top 20%)
	float wavePeakMask = saturate((normalizedHeight - foamThreshold) / max(1.0f - foamThreshold, 0.01f));
	
	// Apply sharpness and intensity
	wavePeakMask = pow(wavePeakMask, foamSharpness);
	
	return wavePeakMask * foamIntensity;
}

#endif // __WATER_FOAM_HLSLI__
