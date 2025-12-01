#ifndef __GERSTNER_WAVES_HLSLI__
#define __GERSTNER_WAVES_HLSLI__

#include "Common/Game.hlsli"

// Realistic Ocean Wave System - Gerstner Wave Implementation
//
// Based on GPU Gems Chapter 1 "Effective Water Simulation from Physical Models" by Mark Finch:
// https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models
//
// And Catlike Coding's Gerstner Wave tutorial:
// https://catlikecoding.com/unity/tutorials/flow/waves/
//
// Key physical principles implemented:
// 1. Deep water dispersion relation: ω² = g·k where ω is angular frequency, g is gravity, k is wave number
// 2. Phase speed: c = √(g/k) = √(g·λ/2π) - longer waves travel faster
// 3. Gerstner wave motion: particles move in circles, creating sharp crests and flat troughs
// 4. Steepness limit: sum(Q·k·A) ≤ 1 prevents wave looping
// 5. Normal calculation via Catlike Coding's accumulated tangent/binormal method

static const float UW_PI = 3.14159265f;
static const float UW_TWO_PI = 6.28318530f;
static const float UW_GRAVITY = 9.81f;  // m/s²

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

struct GerstnerWave
{
	float2 direction;      // Normalized wave travel direction
	float amplitude;       // Wave height (Skyrim units)
	float wavelength;      // Crest-to-crest distance (Skyrim units)
	float waveNumber;      // k = 2π/λ (inverse Skyrim units)
	float angularFrequency;// ω = √(g·k) (rad/s) - deep water dispersion
	float phaseSpeed;      // c = ω/k (Skyrim units/s)
	float steepness;       // Q parameter: 0 = sine wave, 1 = sharp crest
	float phaseOffset;     // Initial phase offset for variation
};

// Initialize a Gerstner wave with physically accurate parameters
// wavelengthMeters: desired wavelength in real-world meters
// amplitudeMeters: desired amplitude in real-world meters
// Returns wave with all parameters in game units
GerstnerWave CreateGerstnerWave(float2 direction, float wavelengthMeters, float amplitudeMeters, float steepness, float phaseOffset)
{
	GerstnerWave wave;
	
	wave.direction = normalize(direction);
	
	// Convert to game units using Game.hlsli constant
	wave.wavelength = wavelengthMeters * M_TO_GAME_UNIT;
	wave.amplitude = amplitudeMeters * M_TO_GAME_UNIT;
	
	// Wave number in game unit space: k = 2π / λ
	wave.waveNumber = UW_TWO_PI / wave.wavelength;
	
	// Deep water dispersion relation: ω = √(g·k)
	// Convert gravity to game units: g_game = g_meters * meters_to_game_unit
	float gravityGame = UW_GRAVITY * M_TO_GAME_UNIT;
	wave.angularFrequency = sqrt(gravityGame * wave.waveNumber);
	
	// Phase speed: c = ω/k = √(g/k)
	wave.phaseSpeed = wave.angularFrequency / wave.waveNumber;
	
	// Steepness (Q parameter) - clamped to prevent looping
	// Maximum safe Q for single wave = 1/(k·A)
	wave.steepness = steepness;
	
	wave.phaseOffset = phaseOffset;
	
	return wave;
}

// Evaluate Gerstner wave displacement at a position
// From GPU Gems Equation 9:
// P(x,y,t) = [x + Q·A·Dx·cos(k·D·(x,y) - ωt + φ),
//             y + Q·A·Dy·cos(k·D·(x,y) - ωt + φ),
//             A·sin(k·D·(x,y) - ωt + φ)]
float3 EvaluateGerstnerWave(GerstnerWave wave, float2 position, float timeSeconds)
{
	// Phase: f = k·(D·position) - ω·t + φ
	float phase = wave.waveNumber * dot(wave.direction, position) 
	            - wave.angularFrequency * timeSeconds 
	            + wave.phaseOffset;
	
	float sinPhase, cosPhase;
	sincos(phase, sinPhase, cosPhase);
	
	// Horizontal displacement pulls vertices toward crests
	// This creates sharp peaks and flat troughs characteristic of ocean waves
	float QA = wave.steepness * wave.amplitude;
	
	return float3(
		wave.direction.x * QA * cosPhase,  // X displacement
		wave.direction.y * QA * cosPhase,  // Y displacement (horizontal)
		wave.amplitude * sinPhase          // Z displacement (vertical)
	);
}

// Calculate tangent and binormal contributions for normal computation
// From GPU Gems Equations 10-12:
// Tangent (∂P/∂x) and Binormal (∂P/∂y) derivatives
void EvaluateGerstnerWaveTangents(GerstnerWave wave, float2 position, float timeSeconds, 
                                   inout float3 tangent, inout float3 binormal)
{
	float phase = wave.waveNumber * dot(wave.direction, position) 
	            - wave.angularFrequency * timeSeconds 
	            + wave.phaseOffset;
	
	float sinPhase, cosPhase;
	sincos(phase, sinPhase, cosPhase);
	
	float Dx = wave.direction.x;
	float Dy = wave.direction.y;
	float kA = wave.waveNumber * wave.amplitude;
	float QkA = wave.steepness * kA;
	
	// Tangent accumulation (partial derivative in X direction)
	// T = [1 - Dx²·Q·k·A·sin(f), -Dx·Dy·Q·k·A·sin(f), Dx·k·A·cos(f)]
	tangent.x += -Dx * Dx * QkA * sinPhase;
	tangent.y += -Dx * Dy * QkA * sinPhase;
	tangent.z += Dx * kA * cosPhase;
	
	// Binormal accumulation (partial derivative in Y direction)
	// B = [-Dx·Dy·Q·k·A·sin(f), 1 - Dy²·Q·k·A·sin(f), Dy·k·A·cos(f)]
	binormal.x += -Dx * Dy * QkA * sinPhase;
	binormal.y += -Dy * Dy * QkA * sinPhase;
	binormal.z += Dy * kA * cosPhase;
}

// ============================================================================
// CONSTANT BUFFER - Wave parameters from CPU
// ============================================================================

#define UNIFIED_WATER_HAS_PER_FRAME_CBUFFER 1
cbuffer UnifiedWaterPerFrame : register(b7)
{
	// Main wave controls
	float WaveIntensity : packoffset(c0.x);      // Master wave strength (0-1)
	float WaveAmplitude : packoffset(c0.y);      // Amplitude multiplier for all waves
	float WaveSpeed : packoffset(c0.z);          // Speed multiplier (1.0 = physically accurate)
	float WaveSteepness : packoffset(c0.w);      // Global steepness multiplier
	
	// Time synchronization
	float GameTimeHours : packoffset(c1.x);
	float RealTimeSeconds : packoffset(c1.y);
	float TimeScale : packoffset(c1.z);
	float CellWorldSize : packoffset(c1.w);
	float PrevGameTimeHours : packoffset(c2.x);
	float PrevRealTimeSeconds : packoffset(c2.y);
	float PrevTimeScale : packoffset(c2.z);
	float EnableLightingOverrides : packoffset(c2.w);
	
	// Fresnel and reflection
	float FresnelBias : packoffset(c3.x);
	float FresnelPower : packoffset(c3.y);
	float ReflectionStrength : packoffset(c3.z);
	float RefractionStrength : packoffset(c3.w);
	
	// Water optical properties
	float WaterTransparency : packoffset(c4.x);
	float AbsorptionDensity : packoffset(c4.y);
	float ScatteringCoeff : packoffset(c4.z);
	float SpecularIntensity : packoffset(c4.w);
	
	// Sun specular
	float SunSpecularPower : packoffset(c5.x);
	float SunSpecularMagnitude : packoffset(c5.y);
	float SunSparklePower : packoffset(c5.z);
	float SunSparkleMagnitude : packoffset(c5.w);
	float SpecularRadius : packoffset(c6.x);
	float SpecularBrightness : packoffset(c6.y);
	
	// Fog
	float AboveWaterFogDistNear : packoffset(c6.z);
	float AboveWaterFogDistFar : packoffset(c6.w);
	float AboveWaterFogAmount : packoffset(c7.x);
	float UnderwaterFogDistNear : packoffset(c7.y);
	float UnderwaterFogDistFar : packoffset(c7.z);
	float UnderwaterFogAmount : packoffset(c7.w);
	
	// Depth controls
	float DepthReflections : packoffset(c8.x);
	float DepthRefractions : packoffset(c8.y);
	float DepthNormals : packoffset(c8.z);
	float DepthSpecularLighting : packoffset(c8.w);
	
	// Wave layer contribution weights
	float WavePrimaryContribution : packoffset(c9.x);    // Primary swell weight
	float WaveSecondaryContribution : packoffset(c9.y);  // Secondary wave weight
	float WaveDetailContribution : packoffset(c9.z);     // Fine detail weight
	float WavePrimarySpeed : packoffset(c9.w);           // Primary speed mult
	float WaveSecondarySpeed : packoffset(c10.x);        // Secondary speed mult
	float WaveDetailSpeed : packoffset(c10.y);           // Detail speed mult
	float WaveDirectionBlend : packoffset(c10.z);        // Wind direction influence
	float TriVisualizerEnabled : packoffset(c10.w);      // Debug wireframe
	
	// Individual wave parameters (wavelength & amplitude in METERS for intuitive editing)
	// Wave 1: Primary ocean swell (largest, slowest)
	float Wave1Amplitude : packoffset(c11.x);     // Amplitude in meters (typ. 0.3-1.0m)
	float Wave1Wavelength : packoffset(c11.y);    // Wavelength in meters (typ. 30-100m)
	float Wave1Steepness : packoffset(c11.z);     // Steepness 0-1 (typ. 0.3-0.5)
	float Wave1AngleOffset : packoffset(c11.w);   // Direction offset radians
	
	// Wave 2: Secondary swell (medium)
	float Wave2Amplitude : packoffset(c12.x);     // typ. 0.15-0.5m
	float Wave2Wavelength : packoffset(c12.y);    // typ. 15-40m
	float Wave2Steepness : packoffset(c12.z);
	float Wave2AngleOffset : packoffset(c12.w);
	
	// Wave 3: Wind waves (smaller, faster)
	float Wave3Amplitude : packoffset(c13.x);     // typ. 0.08-0.25m
	float Wave3Wavelength : packoffset(c13.y);    // typ. 8-20m
	float Wave3Steepness : packoffset(c13.z);
	float Wave3AngleOffset : packoffset(c13.w);
	
	// Wave 4: Chop (short period)
	float Wave4Amplitude : packoffset(c14.x);     // typ. 0.03-0.1m
	float Wave4Wavelength : packoffset(c14.y);    // typ. 3-8m
	float Wave4Steepness : packoffset(c14.z);
	float Wave4AngleOffset : packoffset(c14.w);
	
	// Wave 5: Fine ripples
	float Wave5Amplitude : packoffset(c15.x);     // typ. 0.01-0.05m
	float Wave5Wavelength : packoffset(c15.y);    // typ. 1-4m
	float Wave5Steepness : packoffset(c15.z);
	float Wave5AngleOffset : packoffset(c15.w);
	
	// Wave 6: Micro detail
	float Wave6Amplitude : packoffset(c16.x);     // typ. 0.005-0.02m
	float Wave6Wavelength : packoffset(c16.y);    // typ. 0.5-2m
	float Wave6Steepness : packoffset(c16.z);
	float Wave6AngleOffset : packoffset(c16.w);
	
	// Tessellation
	float TessellationEnabled : packoffset(c17.x);
	float WaveFadeStart : packoffset(c17.y);       // Distance where waves start fading
	float WaveFadeEnd : packoffset(c17.z);         // Distance where waves fully fade
	float TessPadding3 : packoffset(c17.w);
	
	// Player ripples
	float PlayerPosX : packoffset(c19.x);
	float PlayerPosY : packoffset(c19.y);
	float PlayerPosZ : packoffset(c19.z);
	float PlayerSpeed : packoffset(c19.w);
	float PlayerInWater : packoffset(c20.x);
	float RippleStrength : packoffset(c20.y);
	float RippleRadius : packoffset(c20.z);
	float RippleWaveSpeed : packoffset(c20.w);
	float RippleWaveFreq1 : packoffset(c21.x);
	float RippleWaveFreq2 : packoffset(c21.y);
	float RippleWaveFreq3 : packoffset(c21.z);
	float RippleNormalStrength : packoffset(c21.w);
	
	// Foam System
	float FoamEnabled : packoffset(c22.x);
	float FoamIntensity : packoffset(c22.y);
	float FoamThreshold : packoffset(c22.z);
	float FoamSharpness : packoffset(c22.w);
}

cbuffer UnifiedWaterPerTile : register(b8)
{
	float4 PrevData : packoffset(c0);
	float4 TileData : packoffset(c1);
}

// ============================================================================
// PROCEDURAL NOISE - For organic wave variation
// ============================================================================

float hash(float2 p)
{
	float h = dot(p, float2(127.1, 311.7));
	return frac(sin(h) * 43758.5453123);
}

float noise2D(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	float2 u = f * f * (3.0 - 2.0 * f);  // Smoothstep
	
	float a = hash(i);
	float b = hash(i + float2(1.0, 0.0));
	float c = hash(i + float2(0.0, 1.0));
	float d = hash(i + float2(1.0, 1.0));
	
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// ============================================================================
// WAVE OUTPUT STRUCTURE
// ============================================================================

struct WaveSample
{
	float3 displacement;      // XYZ offset in game units
	float3 normal;            // Surface normal (full detail for specular/reflections)
	float3 geometricNormal;   // Softer normal for diffuse lighting (prevents distortion)
	float2 primaryDirection;  // Dominant wave direction for texture scrolling
	float shoreInfluence;     // Shore proximity factor (future use)
	float shoreDistance;      // Distance to shore (future use)
};

// ============================================================================
// MAIN WAVE CALCULATION
// ============================================================================

// Realistic multi-layer Gerstner wave composition
// Uses 6 waves across different frequency bands for natural ocean appearance:
// - Primary swell: Long-period waves from distant weather systems
// - Secondary swell: Shorter swells that travel with primary
// - Wind waves: Medium waves generated by local wind
// - Chop: Short-period waves that add texture
// - Ripples: Fine surface detail
// - Micro detail: Highest frequency for close-up views

WaveSample CalculateWaterDisplacement(
	float2 worldPos,           // Absolute world position (Skyrim units)
	float2 textureDims,        // Texture dimensions (unused, kept for interface)
	float2 texCoordOffset,     // Texture offset (unused)
	float waveIntensity,       // Master intensity (0-1)
	float amplitudeMult,       // Amplitude multiplier
	float speedMult,           // Speed multiplier
	float steepnessMult,       // Steepness multiplier
	float timeSeconds,         // Current time
	float dayPhase,            // Day cycle phase for variation
	float2 flowBiasDir,        // Flow direction bias (rivers)
	float flowBiasWeight,      // Flow direction weight
	bool usePreviousFrame,     // For motion vectors
	float cameraDistance = 0.0f // Distance from camera for LOD fadeout (0 = no fadeout)
)
{
	WaveSample result;
	result.displacement = float3(0.0f, 0.0f, 0.0f);
	result.normal = float3(0.0f, 0.0f, 1.0f);
	result.geometricNormal = float3(0.0f, 0.0f, 1.0f);
	result.primaryDirection = float2(0.0f, 1.0f);
	result.shoreInfluence = 0.0f;
	result.shoreDistance = 10000.0f;
	
	if (waveIntensity <= 0.001f) {
		return result;
	}
	
	// Distance-based wave fadeout for very distant water tiles
	// This improves performance and prevents artifacts where tessellation is minimal
	// Configurable via UI: WaveFadeStart and WaveFadeEnd (game units)
	float distanceFade = 1.0f;
	if (cameraDistance > 0.0f && WaveFadeEnd > WaveFadeStart) {
		distanceFade = 1.0f - saturate((cameraDistance - WaveFadeStart) / (WaveFadeEnd - WaveFadeStart));
		// Smoothstep for gradual transition
		distanceFade = distanceFade * distanceFade * (3.0f - 2.0f * distanceFade);
		
		// Early out if fully faded
		if (distanceFade <= 0.001f) {
			return result;
		}
	}
	
	// Base wind direction - default southwest to northeast (typical for northern hemisphere)
	float2 windDir = normalize(float2(0.707f, 0.707f));
	
	// Apply flow bias for rivers/streams
	if (flowBiasWeight > 0.01f) {
		windDir = normalize(lerp(windDir, flowBiasDir, flowBiasWeight));
	}
	
	result.primaryDirection = windDir;
	
	// Read wave parameters from cbuffer
	// Parameters are in METERS for intuitive editing
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
	
	// Contribution weights per layer
	float contributions[6] = {
		max(WavePrimaryContribution, 0.0f),
		max(WaveSecondaryContribution, 0.0f),
		max(WaveDetailContribution, 0.0f),
		max(WaveDetailContribution * 0.7f, 0.0f),
		max(WaveDetailContribution * 0.5f, 0.0f),
		max(WaveDetailContribution * 0.3f, 0.0f)
	};
	
	// Speed multipliers per layer (longer waves should move faster per dispersion)
	float speedMults[6] = {
		max(WavePrimarySpeed, 0.1f),
		max(WaveSecondarySpeed, 0.1f),
		max(WaveDetailSpeed, 0.1f),
		max(WaveDetailSpeed * 1.1f, 0.1f),
		max(WaveDetailSpeed * 1.2f, 0.1f),
		max(WaveDetailSpeed * 1.3f, 0.1f)
	};
	
	// Phase offsets for temporal variation (prevent repetitive patterns)
	float phaseOffsets[6] = {
		0.0f,
		dayPhase * 0.3f + 2.094f,
		dayPhase * 0.5f + 4.189f,
		dayPhase * 0.7f + 1.047f,
		dayPhase * 0.9f + 5.236f,
		dayPhase * 1.1f + 3.142f
	};
	
	// Build waves with proper physical parameters
	GerstnerWave waves[6];
	
	// Track cumulative Q*k*A to prevent looping (must stay < 1)
	float cumulativeQkA = 0.0f;
	const float MAX_QKA = 0.9f;  // Safety margin below 1.0
	
	[unroll]
	for (int i = 0; i < 6; ++i) {
		// Skip waves with negligible contribution
		if (contributions[i] < 0.001f || amplitudesM[i] < 0.0001f) {
			waves[i].amplitude = 0.0f;
			waves[i].steepness = 0.0f;
			waves[i].waveNumber = 1.0f;
			waves[i].angularFrequency = 1.0f;
			waves[i].direction = windDir;
			waves[i].phaseOffset = 0.0f;
			waves[i].wavelength = 1.0f;
			waves[i].phaseSpeed = 1.0f;
			continue;
		}
		
		// Create rotation for wave direction spread
		// Each wave has slightly different direction for realistic interference
		float angle = angleOffsets[i];
		float cosA, sinA;
		sincos(angle, sinA, cosA);
		float2 waveDir = float2(
			windDir.x * cosA - windDir.y * sinA,
			windDir.x * sinA + windDir.y * cosA
		);
		
		// Create wave with physical parameters
		// Apply distance fade to amplitude for distant water tiles
		waves[i] = CreateGerstnerWave(
			waveDir,
			wavelengthsM[i],
			amplitudesM[i] * contributions[i] * waveIntensity * amplitudeMult * distanceFade,
			steepnesses[i] * steepnessMult,
			phaseOffsets[i]
		);
		
		// Apply speed multiplier to angular frequency
		waves[i].angularFrequency *= speedMult * speedMults[i];
		
		// Enforce cumulative steepness limit to prevent triangle holes
		float thisQkA = waves[i].steepness * waves[i].waveNumber * waves[i].amplitude;
		if (cumulativeQkA + thisQkA > MAX_QKA && thisQkA > 0.0001f) {
			float allowedQkA = max(0.0f, MAX_QKA - cumulativeQkA);
			float scaleFactor = allowedQkA / thisQkA;
			waves[i].steepness *= scaleFactor;
			thisQkA = allowedQkA;
		}
		cumulativeQkA += thisQkA;
	}
	
	// Evaluate all waves and accumulate displacement + tangent frame
	// Using Catlike Coding's method: accumulate derivative offsets, then cross product
	float3 totalDisp = float3(0.0f, 0.0f, 0.0f);
	
	// Tangent/binormal start as flat surface basis vectors
	// Wave contributions are ADDED to these (the 1s in the diagonal are implicit)
	float3 tangent = float3(1.0f, 0.0f, 0.0f);
	float3 binormal = float3(0.0f, 1.0f, 0.0f);
	
	// Also track a softer version for diffuse lighting (only uses primary waves)
	float3 softTangent = float3(1.0f, 0.0f, 0.0f);
	float3 softBinormal = float3(0.0f, 1.0f, 0.0f);
	
	[unroll]
	for (int j = 0; j < 6; ++j) {
		if (waves[j].amplitude > 0.0001f) {
			totalDisp += EvaluateGerstnerWave(waves[j], worldPos, timeSeconds);
			EvaluateGerstnerWaveTangents(waves[j], worldPos, timeSeconds, tangent, binormal);
			
			// Only primary waves (0-2) contribute to soft/geometric normal
			// This prevents high-frequency detail from distorting diffuse lighting
			if (j < 3) {
				EvaluateGerstnerWaveTangents(waves[j], worldPos, timeSeconds, softTangent, softBinormal);
			}
		}
	}
	
	// Compute full-detail normal via cross product (Catlike Coding method)
	// Normal = cross(binormal, tangent) gives us the surface normal
	float3 waveNormal = normalize(cross(binormal, tangent));
	
	// Ensure normal points upward (Z positive in our coordinate system)
	if (waveNormal.z < 0.0f) {
		waveNormal = -waveNormal;
	}
	
	// Compute softer geometric normal for diffuse lighting
	float3 geoNormal = normalize(cross(softBinormal, softTangent));
	if (geoNormal.z < 0.0f) {
		geoNormal = -geoNormal;
	}
	
	// Clamp displacement to prevent extreme values
	// With larger waves (up to 80cm), we need larger clamps
	// Horizontal displacement should still be conservative to prevent mesh tearing
	const float maxHorizDisp = 25.0f;  // ~36cm - conservative for mesh stability
	const float maxVertDisp = 100.0f;  // ~143cm - allows for larger swells
	
	totalDisp.xy = clamp(totalDisp.xy, -maxHorizDisp, maxHorizDisp);
	totalDisp.z = clamp(totalDisp.z, -maxVertDisp, maxVertDisp);
	
	result.displacement = totalDisp;
	result.normal = waveNormal;
	result.geometricNormal = geoNormal;
	
	return result;
}

// ============================================================================
// WAVE SELF-SHADOWING
// ============================================================================
// Approximates self-shadowing of waves without requiring shadow map modifications.
// This technique traces along the light direction using wave height information
// to determine if nearby wave crests would occlude the current point.
// Based on horizon-based ambient occlusion concepts applied to wave geometry.

float CalculateWaveSelfShadow(
	float2 worldPos,          // Current world position XY
	float currentHeight,      // Current wave height at this position
	float3 lightDir,          // Sun direction (pointing toward sun)
	float waveIntensity,      // Master wave intensity
	float amplitudeMult,      // Amplitude multiplier
	float timeSeconds,        // Current time
	float dayPhase            // Day phase for consistency
)
{
	// Skip if waves are disabled or light is directly overhead
	if (waveIntensity <= 0.01f || lightDir.z > 0.95f) {
		return 1.0f;
	}
	
	// Project light direction onto XY plane for tracing
	float2 lightDirXY = lightDir.xy;
	float lightDirXYLen = length(lightDirXY);
	if (lightDirXYLen < 0.01f) {
		return 1.0f;  // Light is nearly vertical
	}
	lightDirXY /= lightDirXYLen;
	
	// Calculate how much height we gain per unit distance toward the light
	// tan(elevation) = z / sqrt(x² + y²)
	float lightElevationTan = lightDir.z / lightDirXYLen;
	
	// Sample parameters - trace toward the light to find occluders
	const int NUM_SAMPLES = 4;
	const float MAX_TRACE_DIST = 150.0f;  // Maximum trace distance in game units (~2m)
	const float STEP_SIZE = MAX_TRACE_DIST / float(NUM_SAMPLES);
	
	float shadow = 1.0f;
	float baseHeight = currentHeight;
	
	// Trace toward the light source
	[unroll]
	for (int i = 1; i <= NUM_SAMPLES; i++) {
		float dist = float(i) * STEP_SIZE;
		float2 samplePos = worldPos + lightDirXY * dist;
		
		// Calculate expected height if no occlusion (height increases toward sun)
		float expectedHeight = baseHeight + dist * lightElevationTan;
		
		// Sample wave height at this position (simplified - just primary waves)
		// Using a simplified calculation for performance
		float2 windDir = normalize(float2(0.707f, 0.707f));
		
		// Sample primary wave
		GerstnerWave wave1 = CreateGerstnerWave(
			windDir,
			max(Wave1Wavelength, 1.0f),
			max(Wave1Amplitude, 0.0f) * waveIntensity * amplitudeMult,
			saturate(Wave1Steepness),
			0.0f
		);
		float3 disp1 = EvaluateGerstnerWave(wave1, samplePos, timeSeconds);
		
		// Sample secondary wave with offset direction
		float angle2 = Wave2AngleOffset;
		float cosA2, sinA2;
		sincos(angle2, sinA2, cosA2);
		float2 waveDir2 = float2(windDir.x * cosA2 - windDir.y * sinA2, windDir.x * sinA2 + windDir.y * cosA2);
		GerstnerWave wave2 = CreateGerstnerWave(
			waveDir2,
			max(Wave2Wavelength, 0.5f),
			max(Wave2Amplitude, 0.0f) * waveIntensity * amplitudeMult * WaveSecondaryContribution,
			saturate(Wave2Steepness),
			dayPhase * 0.3f + 2.094f
		);
		float3 disp2 = EvaluateGerstnerWave(wave2, samplePos, timeSeconds);
		
		float sampleHeight = disp1.z + disp2.z;
		
		// If the sampled wave is higher than expected height, it's blocking light
		float occlusion = sampleHeight - expectedHeight;
		if (occlusion > 0.0f) {
			// Soft shadow falloff based on how much the wave exceeds expected height
			float shadowStrength = saturate(occlusion / (Wave1Amplitude * M_TO_GAME_UNIT * 0.5f));
			// Closer samples have more influence (soft penumbra)
			float distanceFalloff = 1.0f - float(i - 1) / float(NUM_SAMPLES);
			shadow = min(shadow, 1.0f - shadowStrength * distanceFalloff * 0.7f);
		}
	}
	
	// Smooth the shadow to prevent harsh transitions
	return lerp(shadow, 1.0f, 0.3f);
}

#endif // __GERSTNER_WAVES_HLSLI__
