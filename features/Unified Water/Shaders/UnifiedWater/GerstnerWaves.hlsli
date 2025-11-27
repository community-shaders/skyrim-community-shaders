#ifndef __GERSTNER_WAVES_HLSLI__
#define __GERSTNER_WAVES_HLSLI__

// Unified Gerstner Wave System for Enhanced Water Rendering
//
// This system anchors wave motion to both world position and in-game time to prevent spatial
// discontinuities while still allowing smooth temporal animation.
//
// Based on GPU Gems Chapter 1. Effective Water Simulation from Physical Models:
// https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models
// Catlike Coding Gerstner wave implementation: https://catlikecoding.com/unity/tutorials/flow/waves/

static const float UW_PI = 3.14159265f;
static const float UW_TWO_PI = 6.28318530f;

float WrapUnifiedPhase(float phase)
{
	float wrapped = fmod(phase, UW_TWO_PI);
	return wrapped < 0.0f ? wrapped + UW_TWO_PI : wrapped;
}

float ComputeWaveTimeSeconds(float gameTimeHours, float realTimeSeconds)
{
	float gameSeconds = gameTimeHours * 3600.0f;
	float combined = gameSeconds + realTimeSeconds;
	return frac(combined / 65536.0f) * 65536.0f;
}

float ComputeWaveDayPhase(float gameTimeHours)
{
	float dayFraction = frac(gameTimeHours / 24.0f);
	return dayFraction * UW_TWO_PI;
}

struct UnifiedWave
{
	float2 direction;
	float amplitude;
	float waveNumber;
	float angularVelocity;
	float steepness;
	float phaseOffset;
};

// Gerstner Wave Evaluation
// Q (steepness) controls sharpness: 0 = sine wave, 1 = sharpest before looping
float3 EvaluateUnifiedWave(UnifiedWave wave, float2 position, float timeSeconds)
{
	// Calculate wave phase: f = k * (D · position - c * t)
	float spatialPhase = dot(wave.direction, position) * wave.waveNumber + wave.phaseOffset;
	float phase = WrapUnifiedPhase(spatialPhase - wave.angularVelocity * timeSeconds);

	float sineValue;
	float cosineValue;
	sincos(phase, sineValue, cosineValue);

	// Gerstner wave displacement with steepness parameter
	float QA = wave.steepness * wave.amplitude;

	// Gerstner waves create circular motion:
	// Horizontal displacement = D * (Q*A) * cos(f)
	// Vertical displacement = A * sin(f)
	return float3(
		wave.direction.x * QA * cosineValue,  // X displacement
		wave.direction.y * QA * cosineValue,  // Y displacement (Z in world)
		wave.amplitude * sineValue            // Vertical (Y in world)
	);
}

// Calculate Gerstner wave tangent and binormal for normal calculation
// From GPU Gems: Tangent/Binormal derivatives for proper surface orientation
void EvaluateUnifiedWaveTangents(UnifiedWave wave, float2 position, float timeSeconds, 
                                  inout float3 tangent, inout float3 binormal)
{
	float spatialPhase = dot(wave.direction, position) * wave.waveNumber + wave.phaseOffset;
	float phase = WrapUnifiedPhase(spatialPhase - wave.angularVelocity * timeSeconds);

	float sineValue;
	float cosineValue;
	sincos(phase, sineValue, cosineValue);

	float steepnessSin = wave.steepness * sineValue;
	float steepnessCos = wave.steepness * cosineValue;
	
	float Dx = wave.direction.x;
	float Dz = wave.direction.y;  // Note: our Y is world Z
	
	// Tangent (partial derivative in X direction)
	// T = [1 - Dx² * S * sin(f), Dx * S * cos(f), -Dx * Dz * S * sin(f)]
	tangent.x += -Dx * Dx * steepnessSin;  // Accumulated, so subtract from 1 later
	tangent.y += Dx * steepnessCos;
	tangent.z += -Dx * Dz * steepnessSin;
	
	// Binormal (partial derivative in Z direction)  
	// B = [-Dx * Dz * S * sin(f), Dz * S * cos(f), 1 - Dz² * S * sin(f)]
	binormal.x += -Dx * Dz * steepnessSin;
	binormal.y += Dz * steepnessCos;
	binormal.z += -Dz * Dz * steepnessSin;  // Accumulated, so subtract from 1 later
}

#define UNIFIED_WATER_HAS_PER_FRAME_CBUFFER 1
cbuffer UnifiedWaterPerFrame : register(b7)
{
	float WaveIntensity : packoffset(c0.x);
	float WaveAmplitude : packoffset(c0.y);
	float WaveSpeed : packoffset(c0.z);
	float WaveSteepness : packoffset(c0.w);
	float GameTimeHours : packoffset(c1.x);
	float RealTimeSeconds : packoffset(c1.y);
	float TimeScale : packoffset(c1.z);
	float CellWorldSize : packoffset(c1.w);
	float PrevGameTimeHours : packoffset(c2.x);
	float PrevRealTimeSeconds : packoffset(c2.y);
	float PrevTimeScale : packoffset(c2.z);
	float FoamIntensity : packoffset(c2.w);
	float FoamShoreStrength : packoffset(c3.x);
	float FoamCrestStrength : packoffset(c3.y);
	float FoamTurbulenceStrength : packoffset(c3.z);
	float FoamFlowSpeedBase : packoffset(c3.w);
	float FoamFlowSpeedRange : packoffset(c4.x);
	float FoamShoreBoost : packoffset(c4.y);
	float FoamSwirlStrength : packoffset(c4.z);
	float FoamSwirlEnergyScale : packoffset(c4.w);
	float WavePrimaryContribution : packoffset(c5.x);
	float WaveSecondaryContribution : packoffset(c5.y);
	float WaveDetailContribution : packoffset(c5.z);
	float WavePrimarySpeed : packoffset(c5.w);
	float WaveSecondarySpeed : packoffset(c6.x);
	float WaveDetailSpeed : packoffset(c6.y);
	float WaveDirectionBlend : packoffset(c6.z);
	float TriVisualizerEnabled : packoffset(c6.w);
	
	// Wave 1 (Primary) - Large swells
	float Wave1Amplitude : packoffset(c7.x);
	float Wave1Wavelength : packoffset(c7.y);
	float Wave1Steepness : packoffset(c7.z);
	float Wave1AngleOffset : packoffset(c7.w);
	
	// Wave 2 (Secondary) - Medium waves
	float Wave2Amplitude : packoffset(c8.x);
	float Wave2Wavelength : packoffset(c8.y);
	float Wave2Steepness : packoffset(c8.z);
	float Wave2AngleOffset : packoffset(c8.w);
	
	// Wave 3 (Detail) - Small waves
	float Wave3Amplitude : packoffset(c9.x);
	float Wave3Wavelength : packoffset(c9.y);
	float Wave3Steepness : packoffset(c9.z);
	float Wave3AngleOffset : packoffset(c9.w);
	
	// Wave 4 (Fine Ripple 1) - Sub-meter detail
	float Wave4Amplitude : packoffset(c10.x);
	float Wave4Wavelength : packoffset(c10.y);
	float Wave4Steepness : packoffset(c10.z);
	float Wave4AngleOffset : packoffset(c10.w);
	
	// Wave 5 (Fine Ripple 2) - Micro ripples
	float Wave5Amplitude : packoffset(c11.x);
	float Wave5Wavelength : packoffset(c11.y);
	float Wave5Steepness : packoffset(c11.z);
	float Wave5AngleOffset : packoffset(c11.w);
	
	// Wave 6 (Fine Ripple 3) - Tiny surface detail
	float Wave6Amplitude : packoffset(c12.x);
	float Wave6Wavelength : packoffset(c12.y);
	float Wave6Steepness : packoffset(c12.z);
	float Wave6AngleOffset : packoffset(c12.w);
	
	// Tessellation control - when enabled, VS skips wave displacement (DS handles it)
	float TessellationEnabled : packoffset(c13.x);
	float TessPadding1 : packoffset(c13.y);
	float TessPadding2 : packoffset(c13.z);
	float TessPadding3 : packoffset(c13.w);
}

cbuffer UnifiedWaterPerTile : register(b8)
{
	float4 PrevData : packoffset(c0);  // x/y = prev normal, z = prev distance, w = prev segments per axis
	float4 TileData : packoffset(c1);  // x/y = tile cell coords, z = LOD level, w = tile span
}

// Simple hash function for procedural noise
float hash(float2 p)
{
	float h = dot(p, float2(127.1, 311.7));
	return frac(sin(h) * 43758.5453123);
}

// 2D noise function for organic variation
float noise2D(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);

	float2 u = f * f * (3.0 - 2.0 * f);

	float a = hash(i);
	float b = hash(i + float2(1.0, 0.0));
	float c = hash(i + float2(0.0, 1.0));
	float d = hash(i + float2(1.0, 1.0));

	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Multi-octave noise for richer detail
float fractalNoise(float2 p, int octaves)
{
	float value = 0.0;
	float amplitude = 0.5;
	float frequency = 1.0;
	
	for (int i = 0; i < octaves; i++) {
		value += amplitude * noise2D(p * frequency);
		frequency *= 2.0;
		amplitude *= 0.5;
	}
	
	return value;
}

struct WaveSample
{
	float3 displacement;
	float3 normal;
	float2 primaryDirection;
	float shoreInfluence;
	float shoreDistance;
};

WaveSample CalculateWaterDisplacement(float2 worldPos, float2 textureDims, float2 texCoordOffset, float waveIntensity, float amplitudeMult, float speedMult, float steepnessMult, float timeSeconds, float dayPhase, float2 flowBiasDir, float flowBiasWeight, bool usePreviousFrame = false)
{
	if (waveIntensity <= 0.0f) {
		WaveSample zeroSample;
		zeroSample.displacement = float3(0.0f, 0.0f, 0.0f);
		zeroSample.normal = float3(0.0f, 0.0f, 1.0f);
		zeroSample.primaryDirection = float2(0.0f, 1.0f);
		zeroSample.shoreInfluence = 0.0f;
		zeroSample.shoreDistance = 10000.0f;
		return zeroSample;
	}

	// Skyrim unit conversion: 4096 units = 1 cell ≈ 192 meters (from game data)
	// Therefore: 1 Skyrim unit ≈ 0.046875 meters (192m / 4096 units)
	// Inverse: 1 meter ≈ 21.33 Skyrim units
	const float SKYRIM_UNITS_PER_METER = 21.333333f;
	
	UnifiedWave waves[6];

	const float2 defaultWaveDir = float2(-0.70710678f, 0.70710678f);
	float2 primaryDir = defaultWaveDir;
	
	// Create 6 wave directions with varying angles for natural dispersion
	float2 baseDirections[6];
	
	// Wave angle offsets from user parameters (in radians)
	float angleOffsets[6] = {
		Wave1AngleOffset,
		Wave2AngleOffset,
		Wave3AngleOffset,
		Wave4AngleOffset,
		Wave5AngleOffset,
		Wave6AngleOffset
	};
	
	// Generate wave directions by rotating primary direction
	[unroll] for (int dirIdx = 0; dirIdx < 6; ++dirIdx) {
		float angle = angleOffsets[dirIdx];
		float cosA, sinA;
		sincos(angle, sinA, cosA);
		baseDirections[dirIdx] = float2(
			primaryDir.x * cosA - primaryDir.y * sinA,
			primaryDir.x * sinA + primaryDir.y * cosA);
	}
	
	// User-configurable wave parameters (wavelengths in Skyrim units)
	// Convert to meters for physical calculations: wavelength_meters = wavelength_units / SKYRIM_UNITS_PER_METER
	float baseAmplitudesUnits[6] = {
		Wave1Amplitude, Wave2Amplitude, Wave3Amplitude,
		Wave4Amplitude, Wave5Amplitude, Wave6Amplitude
	};
	float baseWaveLengthsUnits[6] = {
		Wave1Wavelength, Wave2Wavelength, Wave3Wavelength,
		Wave4Wavelength, Wave5Wavelength, Wave6Wavelength
	};
	float baseSteepness[6] = {
		Wave1Steepness, Wave2Steepness, Wave3Steepness,
		Wave4Steepness, Wave5Steepness, Wave6Steepness
	};
	
	// Physics constants
	const float gravity = 9.8f; // Earth gravity in m/s²
	
	// Contribution weights for user control (first 3 exposed in UI, rest auto-calculated)
	float contributions[6] = {
		max(WavePrimaryContribution, 0.0f),
		max(WaveSecondaryContribution, 0.0f),
		max(WaveDetailContribution, 0.0f),
		// Detail waves use physics-based amplitude falloff
		max(WaveDetailContribution * 0.65f, 0.0f),  // Wave 4: 65% of detail
		max(WaveDetailContribution * 0.45f, 0.0f),  // Wave 5: 45% of detail
		max(WaveDetailContribution * 0.30f, 0.0f)   // Wave 6: 30% of detail
	};
	
	// Disable waves with negligible contribution to prevent artifacts
	[unroll] for (int i = 0; i < 6; ++i) {
		if (contributions[i] < 0.001f) {
			contributions[i] = 0.0f;
		}
	}
	
	// Speed scaling per wave (first 3 user-controlled, rest use physics)
	float speedScale[6] = {
		max(WavePrimarySpeed, 0.0f),
		max(WaveSecondarySpeed, 0.0f),
		max(WaveDetailSpeed, 0.0f),
		max(WaveDetailSpeed * 1.15f, 0.0f),  // Detail waves slightly faster
		max(WaveDetailSpeed * 1.30f, 0.0f),
		max(WaveDetailSpeed * 1.50f, 0.0f)
	};
	
	// Day-phase variation for temporal diversity (reduces repetition)
	float dayScale[6] = { 1.0f, 1.45f, 2.2f, 3.1f, 4.5f, 6.2f };
	float dayBias[6] = { 0.0f, 2.0943951f, 4.1887903f, 1.5707963f, 3.6651914f, 5.4977871f };

	// Track cumulative steepness to prevent triangle holes from vertex crossover
	// Per GPU Gems: sum of Q*k*A for all waves must not exceed 1 to prevent looping
	float cumulativeSteepnessKA = 0.0f;
	const float MAX_CUMULATIVE_STEEPNESS = 0.85f;  // Conservative limit below 1.0
	
	[unroll] for (int j = 0; j < 6; ++j) {
		waves[j].direction = normalize(baseDirections[j]);
		
		// Convert wavelength from Skyrim units to meters for physics calculations
		float wavelengthMeters = baseWaveLengthsUnits[j] / SKYRIM_UNITS_PER_METER;
		
		// Amplitude in Skyrim units (already correctly scaled for visual output)
		waves[j].amplitude = baseAmplitudesUnits[j] * waveIntensity * amplitudeMult * contributions[j];
		
		// Calculate wave number in Skyrim units: k = 2π / wavelength_units
		float waveNumberUnits = UW_TWO_PI / baseWaveLengthsUnits[j];
		waves[j].waveNumber = waveNumberUnits;
		
		// Physically accurate phase speed using METER wavelength: c = sqrt(g * λ_meters / 2π)
		// Deep water dispersion relation ensures longer waves travel faster
		float phaseSpeedMetersPerSec = sqrt(gravity * wavelengthMeters / UW_TWO_PI);
		
		// Convert phase speed to Skyrim units/sec
		float phaseSpeedUnitsPerSec = phaseSpeedMetersPerSec * SKYRIM_UNITS_PER_METER;
		
		// Angular velocity: ω = k * c (in Skyrim units)
		// User speed multiplier allows artistic control while preserving physics
		waves[j].angularVelocity = waveNumberUnits * phaseSpeedUnitsPerSec * speedMult * speedScale[j];
		
		// Steepness controls wave sharpness (0 = sine, 1 = sharp crest)
		// Calculate this wave's contribution to cumulative steepness: Q*k*A
		float rawSteepness = saturate(baseSteepness[j] * steepnessMult * contributions[j]);
		float thisWaveKA = waveNumberUnits * waves[j].amplitude;
		float thisWaveSteepnessContrib = rawSteepness * thisWaveKA;
		
		// Scale down steepness if cumulative would exceed safe limit
		if (cumulativeSteepnessKA + thisWaveSteepnessContrib > MAX_CUMULATIVE_STEEPNESS && thisWaveSteepnessContrib > 0.001f) {
			float allowedContrib = max(0.0f, MAX_CUMULATIVE_STEEPNESS - cumulativeSteepnessKA);
			float scaleFactor = allowedContrib / thisWaveSteepnessContrib;
			rawSteepness *= scaleFactor;
			thisWaveSteepnessContrib = allowedContrib;
		}
		
		waves[j].steepness = rawSteepness;
		cumulativeSteepnessKA += thisWaveSteepnessContrib;
		
		// Temporal phase offset for variation (reduces repetition)
		waves[j].phaseOffset = dayPhase * dayScale[j] + dayBias[j];
	}

	float3 totalDisplacement = float3(0.0f, 0.0f, 0.0f);
	
	// Initialize tangent and binormal for normal calculation
	// Start with flat plane: tangent = (1, 0, 0), binormal = (0, 0, 1)
	float3 tangent = float3(1.0f, 0.0f, 0.0f);
	float3 binormal = float3(0.0f, 0.0f, 1.0f);

	[unroll] for (int k = 0; k < 6; ++k) {
		// Skip disabled waves to prevent precision artifacts
		if (contributions[k] > 0.0f) {
			totalDisplacement += EvaluateUnifiedWave(waves[k], worldPos, timeSeconds);
			EvaluateUnifiedWaveTangents(waves[k], worldPos, timeSeconds, tangent, binormal);
		}
	}
	
	// Complete the tangent and binormal by adding back the base values
	// (derivatives accumulated the changes, now restore the flat plane base)
	tangent.x += 1.0f;
	binormal.z += 1.0f;
	
	// Calculate normal via cross product: N = normalize(binormal × tangent)
	float3 waveNormal = normalize(cross(binormal, tangent));
	
	// Clamp displacement to prevent extreme values that cause rendering artifacts
	// Horizontal displacement limited more aggressively to prevent triangle holes
	// (typical water mesh has ~64-128 unit vertex spacing)
	const float maxHorizontalDisp = 24.0f;
	const float maxVerticalDisp = 35.0f;
	totalDisplacement.xy = clamp(totalDisplacement.xy, -maxHorizontalDisp, maxHorizontalDisp);
	totalDisplacement.z = clamp(totalDisplacement.z, -maxVerticalDisp, maxVerticalDisp);

	WaveSample sample;
	sample.displacement = totalDisplacement;
	sample.normal = waveNormal;
	sample.primaryDirection = primaryDir;
	sample.shoreInfluence = 0.0f;
	sample.shoreDistance = 10000.0f;
	return sample;
}

#endif // __GERSTNER_WAVES_HLSLI__
