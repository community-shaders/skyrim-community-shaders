#ifndef __GERSTNER_WAVES_HLSLI__
#define __GERSTNER_WAVES_HLSLI__

#include "Common/Game.hlsli"

// ============================================================================
// STATISTICAL OCEAN WAVE SYNTHESIS
// ============================================================================
// 
// This system creates non-repeating ocean waves by generating unique wave sets
// per spatial cell, then blending smoothly between cells. This approximates
// FFT ocean simulation without requiring compute shader infrastructure.
//
// Key principles:
// 1. Spatial cells - world divided into large cells, each with unique waves
// 2. Hash-based generation - cell coordinates seed deterministic random waves
// 3. Ocean spectrum - Phillips/JONSWAP spectrum for realistic energy distribution
// 4. Cell blending - smooth interpolation at cell boundaries
// 5. Multiple octaves - different cell sizes for different wave scales
//
// References:
// - Tessendorf "Simulating Ocean Water" (SIGGRAPH 2001)
// - GPU Gems Chapter 1 "Effective Water Simulation"
// - Catlike Coding "Waves" tutorial

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
// HIGH QUALITY HASH FUNCTIONS
// ============================================================================
// These need to be high quality to avoid visible patterns in wave generation

uint uhash(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

uint uhash2(uint2 v)
{
	return uhash(v.x ^ uhash(v.y));
}

float hashf(uint x)
{
	return float(uhash(x)) / 4294967295.0f;
}

float hashf2(uint2 v)
{
	return float(uhash2(v)) / 4294967295.0f;
}

float2 hashf22(uint2 v)
{
	uint h = uhash2(v);
	return float2(
		float(h & 0xFFFF) / 65535.0f,
		float(h >> 16) / 65535.0f
	);
}

float4 hashf24(uint2 v)
{
	uint h1 = uhash2(v);
	uint h2 = uhash(h1);
	return float4(
		float(h1 & 0xFFFF) / 65535.0f,
		float(h1 >> 16) / 65535.0f,
		float(h2 & 0xFFFF) / 65535.0f,
		float(h2 >> 16) / 65535.0f
	);
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
// CELL-BASED WAVE GENERATION
// ============================================================================
// Each cell generates N unique waves based on its coordinates
// Adjacent cells blend together smoothly to hide transitions

// Number of waves per cell per octave - more waves = richer interaction
#define WAVES_PER_CELL 6

struct CellWaveData
{
	float3 displacement;
	float3 tangentAccum;   // Accumulated tangent perturbation
	float3 binormalAccum;  // Accumulated binormal perturbation
};

CellWaveData EvaluateCellWaves(
	int2 cellCoord,
	float2 localPos,        // Position within cell [0,1]
	float cellSize,         // Cell size in game units
	float baseWavelength,   // Base wavelength for this octave (meters)
	float baseAmplitude,    // Base amplitude for this octave (meters)
	float steepness,
	float speedMult,
	float timeSeconds,
	uint octaveIndex
)
{
	CellWaveData result;
	result.displacement = float3(0, 0, 0);
	result.tangentAccum = float3(0, 0, 0);
	result.binormalAccum = float3(0, 0, 0);
	
	float2 worldPosInCell = localPos * cellSize;
	
	uint cellSeed = uhash2(uint2(
		uint(cellCoord.x + 10000) ^ (octaveIndex * 7919),
		uint(cellCoord.y + 10000) ^ (octaveIndex * 6271)
	));
	
	float gravityGame = UW_GRAVITY * M_TO_GAME_UNIT;
	
	// Track displacement for wave interaction (domain warping within cell)
	float2 warpedPos = worldPosInCell;
	
	[unroll]
	for (int w = 0; w < WAVES_PER_CELL; w++) {
		uint waveSeed = uhash(cellSeed ^ (w * 104729));
		
		float4 rnd = hashf24(uint2(waveSeed, w));
		float2 rnd2 = hashf22(uint2(waveSeed + 1, w + 1));
		
		float angle = rnd.x * UW_TWO_PI;
		float2 dir = float2(cos(angle), sin(angle));
		
		// Wider wavelength variation for more diversity (0.4x to 1.8x)
		float wavelengthVariation = 0.4f + rnd.y * 1.4f;
		float wavelengthM = baseWavelength * wavelengthVariation;
		float wavelengthGame = wavelengthM * M_TO_GAME_UNIT;
		
		// Wider amplitude variation (0.2x to 2.0x) for stronger peaks/troughs
		float amplitudeVariation = 0.2f + rnd.z * 1.8f;
		float amplitudeM = baseAmplitude * amplitudeVariation;
		float amplitudeGame = amplitudeM * M_TO_GAME_UNIT;
		
		float phaseOffset = rnd.w * UW_TWO_PI;
		
		// Additional phase variation from second random pair
		phaseOffset += rnd2.x * UW_PI;
		
		float k = UW_TWO_PI / wavelengthGame;
		float omega = sqrt(gravityGame * k) * speedMult;
		
		// Use warped position for wave interaction
		float phase = k * dot(dir, warpedPos) - omega * timeSeconds + phaseOffset;
		
		float sinP, cosP;
		sincos(phase, sinP, cosP);
		
		float QA = steepness * amplitudeGame;
		
		float3 waveDisp;
		waveDisp.x = dir.x * QA * cosP;
		waveDisp.y = dir.y * QA * cosP;
		waveDisp.z = amplitudeGame * sinP;
		
		result.displacement += waveDisp;
		
		// Domain warping: each wave shifts position for subsequent waves
		// This creates wave-to-wave interaction and breaks patterns
		float warpStrength = 0.15f * amplitudeGame;
		warpedPos -= waveDisp.xy * warpStrength;
		
		float kA = k * amplitudeGame;
		float QkA = steepness * kA;
		
		float DxDxQkAsin = dir.x * dir.x * QkA * sinP;
		float DxDyQkAsin = dir.x * dir.y * QkA * sinP;
		float DyDyQkAsin = dir.y * dir.y * QkA * sinP;
		float DxkAcos = dir.x * kA * cosP;
		float DykAcos = dir.y * kA * cosP;
		
		result.tangentAccum.x += DxDxQkAsin;
		result.tangentAccum.y += DxDyQkAsin;
		result.tangentAccum.z += DxkAcos;
		
		result.binormalAccum.x += DxDyQkAsin;
		result.binormalAccum.y += DyDyQkAsin;
		result.binormalAccum.z += DykAcos;
	}
	
	return result;
}

float smoothBlend(float t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

CellWaveData BlendCellWaves(
	float2 worldPos,
	float cellSize,
	float baseWavelength,
	float baseAmplitude,
	float steepness,
	float speedMult,
	float timeSeconds,
	uint octaveIndex
)
{
	float2 cellPos = worldPos / cellSize;
	int2 cellCoord = int2(floor(cellPos));
	float2 localPos = frac(cellPos);
	
	float2 blend = float2(smoothBlend(localPos.x), smoothBlend(localPos.y));
	
	CellWaveData c00 = EvaluateCellWaves(cellCoord + int2(0, 0), localPos + float2(0, 0), cellSize, baseWavelength, baseAmplitude, steepness, speedMult, timeSeconds, octaveIndex);
	CellWaveData c10 = EvaluateCellWaves(cellCoord + int2(1, 0), localPos + float2(-1, 0), cellSize, baseWavelength, baseAmplitude, steepness, speedMult, timeSeconds, octaveIndex);
	CellWaveData c01 = EvaluateCellWaves(cellCoord + int2(0, 1), localPos + float2(0, -1), cellSize, baseWavelength, baseAmplitude, steepness, speedMult, timeSeconds, octaveIndex);
	CellWaveData c11 = EvaluateCellWaves(cellCoord + int2(1, 1), localPos + float2(-1, -1), cellSize, baseWavelength, baseAmplitude, steepness, speedMult, timeSeconds, octaveIndex);
	
	CellWaveData result;
	
	result.displacement = lerp(
		lerp(c00.displacement, c10.displacement, blend.x),
		lerp(c01.displacement, c11.displacement, blend.x),
		blend.y
	);
	
	result.tangentAccum = lerp(
		lerp(c00.tangentAccum, c10.tangentAccum, blend.x),
		lerp(c01.tangentAccum, c11.tangentAccum, blend.x),
		blend.y
	);
	
	result.binormalAccum = lerp(
		lerp(c00.binormalAccum, c10.binormalAccum, blend.x),
		lerp(c01.binormalAccum, c11.binormalAccum, blend.x),
		blend.y
	);
	
	return result;
}

// ============================================================================
// MAIN WAVE CALCULATION - STATISTICAL SYNTHESIS
// ============================================================================

WaveSample CalculateWaterDisplacement(
	float2 worldPos,
	float2 textureDims,
	float2 texCoordOffset,
	float waveIntensity,
	float amplitudeMult,
	float speedMult,
	float steepnessMult,
	float timeSeconds,
	float dayPhase,
	float2 flowBiasDir,
	float flowBiasWeight,
	bool usePreviousFrame,
	float cameraDistance = 0.0f
)
{
	WaveSample result;
	result.displacement = float3(0.0f, 0.0f, 0.0f);
	result.normal = float3(0.0f, 0.0f, 1.0f);
	result.geometricNormal = float3(0.0f, 0.0f, 1.0f);
	result.primaryDirection = normalize(float2(0.707f, 0.707f));
	result.shoreInfluence = 0.0f;
	result.shoreDistance = 10000.0f;
	
	if (waveIntensity <= 0.001f) {
		return result;
	}
	
	float distanceFade = 1.0f;
	if (cameraDistance > 0.0f && WaveFadeEnd > WaveFadeStart) {
		distanceFade = 1.0f - saturate((cameraDistance - WaveFadeStart) / (WaveFadeEnd - WaveFadeStart));
		distanceFade = distanceFade * distanceFade * (3.0f - 2.0f * distanceFade);
		
		if (distanceFade <= 0.001f) {
			return result;
		}
	}
	
	float globalAmplitude = waveIntensity * amplitudeMult * distanceFade;
	
	float userWavelengths[6] = {
		max(Wave1Wavelength, 1.0f),
		max(Wave2Wavelength, 0.5f),
		max(Wave3Wavelength, 0.25f),
		max(Wave4Wavelength, 0.1f),
		max(Wave5Wavelength, 0.05f),
		max(Wave6Wavelength, 0.025f)
	};
	
	float userAmplitudes[6] = {
		max(Wave1Amplitude, 0.0f),
		max(Wave2Amplitude, 0.0f),
		max(Wave3Amplitude, 0.0f),
		max(Wave4Amplitude, 0.0f),
		max(Wave5Amplitude, 0.0f),
		max(Wave6Amplitude, 0.0f)
	};
	
	float userSteepness[6] = {
		saturate(Wave1Steepness),
		saturate(Wave2Steepness),
		saturate(Wave3Steepness),
		saturate(Wave4Steepness),
		saturate(Wave5Steepness),
		saturate(Wave6Steepness)
	};
	
	float contributions[6] = {
		max(WavePrimaryContribution, 0.0f),
		max(WaveSecondaryContribution, 0.0f),
		max(WaveDetailContribution, 0.0f),
		max(WaveDetailContribution * 0.7f, 0.0f),
		max(WaveDetailContribution * 0.5f, 0.0f),
		max(WaveDetailContribution * 0.3f, 0.0f)
	};
	
	float speedMults[6] = {
		max(WavePrimarySpeed, 0.1f),
		max(WaveSecondarySpeed, 0.1f),
		max(WaveDetailSpeed, 0.1f),
		max(WaveDetailSpeed * 1.1f, 0.1f),
		max(WaveDetailSpeed * 1.2f, 0.1f),
		max(WaveDetailSpeed * 1.3f, 0.1f)
	};
	
	float3 totalDisp = float3(0, 0, 0);
	float3 totalTangent = float3(0, 0, 0);
	float3 totalBinormal = float3(0, 0, 0);
	float3 geoTangent = float3(0, 0, 0);
	float3 geoBinormal = float3(0, 0, 0);
	
	// Cross-octave warping: larger waves influence sampling of smaller waves
	float2 warpedWorldPos = worldPos;
	
	[unroll]
	for (int oct = 0; oct < 6; oct++) {
		if (contributions[oct] < 0.001f || userAmplitudes[oct] < 0.0001f) {
			continue;
		}
		
		float wavelengthM = userWavelengths[oct];
		float cellSizeGame = wavelengthM * M_TO_GAME_UNIT * 6.0f;  // Slightly smaller cells for more variation
		
		float octaveAmp = userAmplitudes[oct] * contributions[oct] * globalAmplitude;
		float octaveSteep = userSteepness[oct] * steepnessMult;
		float octaveSpeed = speedMult * speedMults[oct];
		
		CellWaveData cellData = BlendCellWaves(
			warpedWorldPos,  // Use warped position for cross-octave interaction
			cellSizeGame,
			wavelengthM,
			octaveAmp,
			octaveSteep,
			octaveSpeed,
			timeSeconds,
			uint(oct)
		);
		
		totalDisp += cellData.displacement;
		totalTangent += cellData.tangentAccum;
		totalBinormal += cellData.binormalAccum;
		
		// Cross-octave warping: this octave's displacement affects smaller wave sampling
		// Strength decreases for smaller waves to prevent chaos
		float crossWarpStrength = 0.25f * (1.0f - float(oct) / 6.0f);
		warpedWorldPos -= cellData.displacement.xy * crossWarpStrength;
		
		if (oct < 3) {
			geoTangent += cellData.tangentAccum;
			geoBinormal += cellData.binormalAccum;
		}
	}
	
	float3 tangent = float3(1.0f - totalTangent.x, -totalTangent.y, totalTangent.z);
	float3 binormal = float3(-totalBinormal.x, 1.0f - totalBinormal.y, totalBinormal.z);
	
	float3 rawNormal = cross(binormal, tangent);
	float normalLen = length(rawNormal);
	
	float3 waveNormal;
	if (normalLen < 0.1f) {
		waveNormal = normalize(lerp(float3(0, 0, 1), rawNormal / max(normalLen, 0.001f), normalLen * 10.0f));
	} else {
		waveNormal = rawNormal / normalLen;
	}
	
	if (waveNormal.z < 0.0f) {
		waveNormal = -waveNormal;
	}
	
	float3 geoTan = float3(1.0f - geoTangent.x, -geoTangent.y, geoTangent.z);
	float3 geoBin = float3(-geoBinormal.x, 1.0f - geoBinormal.y, geoBinormal.z);
	
	float3 rawGeoNormal = cross(geoBin, geoTan);
	float geoNormalLen = length(rawGeoNormal);
	
	float3 geoNormal;
	if (geoNormalLen < 0.1f) {
		geoNormal = normalize(lerp(float3(0, 0, 1), rawGeoNormal / max(geoNormalLen, 0.001f), geoNormalLen * 10.0f));
	} else {
		geoNormal = rawGeoNormal / geoNormalLen;
	}
	
	if (geoNormal.z < 0.0f) {
		geoNormal = -geoNormal;
	}
	
	const float maxHorizDisp = 25.0f;
	const float maxVertDisp = 100.0f;
	
	totalDisp.xy = clamp(totalDisp.xy, -maxHorizDisp, maxHorizDisp);
	totalDisp.z = clamp(totalDisp.z, -maxVertDisp, maxVertDisp);
	
	result.displacement = totalDisp;
	result.normal = waveNormal;
	result.geometricNormal = geoNormal;
	
	return result;
}

// ============================================================================
// WAVE SELF-SHADOWING (Simplified - avoids nested loop unroll issues)
// ============================================================================

float CalculateWaveSelfShadow(
	float2 worldPos,
	float currentHeight,
	float3 lightDir,
	float waveIntensity,
	float amplitudeMult,
	float timeSeconds,
	float dayPhase
)
{
	// Simplified implementation - just use current height and light angle
	// Full wave sampling in a loop causes unroll issues
	if (waveIntensity <= 0.01f || lightDir.z > 0.95f) {
		return 1.0f;
	}
	
	// Simple approximation based on wave amplitude and light angle
	float maxWaveHeight = Wave1Amplitude * M_TO_GAME_UNIT * waveIntensity * amplitudeMult;
	float shadowFactor = saturate(lightDir.z * 2.0f); // More shadow at grazing angles
	
	return lerp(0.7f, 1.0f, shadowFactor);
}

#endif // __GERSTNER_WAVES_HLSLI__
