#ifdef __RESHARPER__
#	define VSHADER
// #	define PSHADER
#	define FLOWMAP
#	define SPECULAR
#	define REFRACTIONS
#	define REFLECTIONS
#	define BLEND_NORMALS
#	define DYNAMIC_CUBEMAPS
#	define SKYLIGHTING
#	define WATER_PARALLAX
#	define NORMAL_TEXCOORD
// #	define WADING
#	define UNIFIED_WATER
#endif

// #ifdef LOD
// #undef LOD
// // #define FLOWMAP
// #define SPECULAR
// #undef SIMPLE
// // #define REFRACTIONS
// // #define REFLECTIONS
// #define NORMAL_TEXCOORD
// #endif

#if defined(UNDERWATERMASK)

struct VS_INPUT
{
	float4 Position : POSITION0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION0;
};

#	ifdef VSHADER

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float z = min(1, 1e-4 * max(0, input.Position.z - 70000)) * 0.5 + input.Position.z;
	vsout.Position = float4(input.Position.xy, z, 1);

	return vsout;
}
#	endif
typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#	ifdef PSHADER
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	psout.Color = 1;

	return psout;
}
#	endif
#else

#	include "Common/FrameBuffer.hlsli"
#	include "Common/MotionBlur.hlsli"
#	include "Common/Permutation.hlsli"
#	include "Common/Random.hlsli"
#	include "Common/Color.hlsli"

#	define WATER

#	include "Common/SharedData.hlsli"

#if defined(FLOWMAP)
SamplerState FlowMapSampler : register(s8);
Texture2D<float4> FlowMapTex : register(t8);
#else
SamplerState FlowMapSampler : register(s8);
#endif

struct VS_INPUT
{
#	if defined(SPECULAR) || defined(UNDERWATER) || defined(STENCIL) || defined(SIMPLE)
	float4 Position : POSITION0;
#		if defined(NORMAL_TEXCOORD)
	float2 TexCoord0 : TEXCOORD0;
#		endif
#		if defined(VC)
	float4 Color : COLOR0;
#		endif
#	endif
#	if defined(LOD)
	float4 Position : POSITION0;
#		if defined(VC)
	float4 Color : COLOR0;
#		endif
#	endif
#	if defined(VR)
	uint InstanceID : SV_INSTANCEID;
#	endif  // VR
};

struct VS_OUTPUT
{
#	if defined(SPECULAR) || defined(UNDERWATER)
	float4 HPosition : SV_POSITION0;
#   if !defined(UNIFIED_WATER)
	float4 FogParam : COLOR0;
#	endif
	float4 WPosition : TEXCOORD0;
	float4 TexCoord1 : TEXCOORD1;
	float4 TexCoord2 : TEXCOORD2;
#		if defined(WADING) || (defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS))) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)) || ((defined(SPECULAR) && NUM_SPECULAR_LIGHTS == 0) && defined(FLOWMAP) /*!defined(NORMAL_TEXCOORD) && !defined(BLEND_NORMALS) && !defined(VC)*/)
	float4 TexCoord3 : TEXCOORD3;
#		endif
#		if defined(FLOWMAP)
	nointerpolation float2 TexCoord4 : TEXCOORD4;
#		endif
#		if NUM_SPECULAR_LIGHTS == 0
	float4 MPosition : TEXCOORD5;
#		endif
#	endif

#	if defined(SIMPLE)
	float4 HPosition : SV_POSITION0;
	float4 FogParam : COLOR0;
	float4 WPosition : TEXCOORD0;
	float4 TexCoord1 : TEXCOORD1;
	float4 TexCoord2 : TEXCOORD2;
	float4 MPosition : TEXCOORD5;
#	endif

#	if defined(LOD)
	float4 HPosition : SV_POSITION0;
	float4 FogParam : COLOR0;
	float4 WPosition : TEXCOORD0;
	float4 TexCoord1 : TEXCOORD1;
#	endif

#	if defined(STENCIL)
	float4 HPosition : SV_POSITION0;
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
#	endif

	float4 NormalsScale : TEXCOORD8;
#	if defined(UNIFIED_WATER)
	float4 UnifiedWaveInfo : TEXCOORD9;
	float4 UnifiedWaveNormal : TEXCOORD10;
#	endif
#	if defined(VR)
	float ClipDistance : SV_ClipDistance0;  // o11
	float CullDistance : SV_CullDistance0;  // p11
#	endif  // VR
};

#if defined(UNIFIED_WATER)

// Unified Gerstner Wave System for Enhanced Water Rendering
//
// This system anchors wave motion to both world position and in-game time to prevent spatial
// discontinuities while still allowing smooth temporal animation.

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

// Generate per-cell spatial phase offset for wave variation
// Uses world position to create deterministic but varied phases between cells
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
// Based on GPU Gems Chapter 1. Effective Water Simulation from Physical Models: https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models
// Catlike Coding Gerstner wave implementation: https://catlikecoding.com/unity/tutorials/flow/waves/
float3 EvaluateUnifiedWave(UnifiedWave wave, float2 position, float timeSeconds)
{
	// Calculate wave phase: f = k * (D · position - c * t)
	float spatialPhase = dot(wave.direction, position) * wave.waveNumber + wave.phaseOffset;
	float phase = WrapUnifiedPhase(spatialPhase - wave.angularVelocity * timeSeconds);

	float sineValue;
	float cosineValue;
	sincos(phase, sineValue, cosineValue);

	// Gerstner wave displacement with steepness parameter
	// Q (steepness) controls sharpness: 0 = sine wave, 1 = sharpest before looping
	// Amplitude A = steepness / k (for proper scaling)
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
// Returns: float4(tangent.xyz, binormal.z) - optimized to reduce calculations
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
	
	// Wave 1 (Primary) parameters - Period removed, speed now from physics
	float Wave1Amplitude : packoffset(c7.x);
	float Wave1Wavelength : packoffset(c7.y);
	float Wave1Steepness : packoffset(c7.z);
	// padding c7.w
	
	// Wave 2 (Secondary) parameters
	float Wave2Amplitude : packoffset(c8.x);
	float Wave2Wavelength : packoffset(c8.y);
	float Wave2Steepness : packoffset(c8.z);
	// padding c8.w
	
	// Wave 3 (Detail) parameters
	float Wave3Amplitude : packoffset(c9.x);
	float Wave3Wavelength : packoffset(c9.y);
	float Wave3Steepness : packoffset(c9.z);
	// padding c9.w
	
	// Wave angle offsets (in radians)
	float Wave1AngleOffset : packoffset(c10.x);
	float Wave2AngleOffset : packoffset(c10.y);
	float Wave3AngleOffset : packoffset(c10.z);
	// padding c10.w
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

	UnifiedWave waves[3];

	const float2 defaultWaveDir = float2(-0.70710678f, 0.70710678f);
	float2 primaryDir = defaultWaveDir;
	
	// Create 3 wave directions that harmonize with primary direction
	float2 baseDirections[3];
	
	// Primary wave with user-defined angle offset
	float angle1 = Wave1AngleOffset;
	float cos1, sin1;
	sincos(angle1, sin1, cos1);
	baseDirections[0] = float2(
		primaryDir.x * cos1 - primaryDir.y * sin1,
		primaryDir.x * sin1 + primaryDir.y * cos1);
	
	// Secondary wave at user-defined angle
	float angle2 = Wave2AngleOffset;
	float cos2, sin2;
	sincos(angle2, sin2, cos2);
	baseDirections[1] = float2(
		primaryDir.x * cos2 - primaryDir.y * sin2,
		primaryDir.x * sin2 + primaryDir.y * cos2);

	// Tertiary wave at user-defined angle
	float angle3 = Wave3AngleOffset;
	float cos3, sin3;
	sincos(angle3, sin3, cos3);
	baseDirections[2] = float2(
		primaryDir.x * cos3 - primaryDir.y * sin3,
		primaryDir.x * sin3 + primaryDir.y * cos3);
	
	// User-configurable wave parameters
	float baseAmplitudes[3] = { Wave1Amplitude, Wave2Amplitude, Wave3Amplitude };
	float baseWaveLengths[3] = { Wave1Wavelength, Wave2Wavelength, Wave3Wavelength };
	float baseSteepness[3] = { Wave1Steepness, Wave2Steepness, Wave3Steepness };
	
	// Physics constants
	const float gravity = 9.8f; // Earth gravity in m/s²
	
	float contributions[3] = {
		max(WavePrimaryContribution, 0.0f),
		max(WaveSecondaryContribution, 0.0f),
		max(WaveDetailContribution, 0.0f)
	};
	
	// Completely disable waves with very small contributions to prevent artifacts
	[unroll] for (int i = 0; i < 3; ++i) {
		if (contributions[i] < 0.001f) {
			contributions[i] = 0.0f;
		}
	}
	
	float speedScale[3] = {
		max(WavePrimarySpeed, 0.0f),
		max(WaveSecondarySpeed, 0.0f),
		max(WaveDetailSpeed, 0.0f)
	};
	float dayScale[3] = { 1.0f, 1.45f, 2.2f };
	float dayBias[3] = { 0.0f, 2.0943951f, 4.1887903f };

	[unroll] for (int j = 0; j < 3; ++j) {
		waves[j].direction = normalize(baseDirections[j]);
		waves[j].amplitude = baseAmplitudes[j] * waveIntensity * amplitudeMult * contributions[j];
		
		// Calculate wave number: k = 2π / wavelength
		float waveNumberBase = UW_TWO_PI / baseWaveLengths[j];
		waves[j].waveNumber = waveNumberBase;
		
		// Physically accurate phase speed: c = sqrt(g / k) = sqrt(g * wavelength / 2π)
		// This relationship ensures longer waves travel faster (dispersion relation for deep water)
		float phaseSpeed = sqrt(gravity * baseWaveLengths[j] / UW_TWO_PI);
		
		// Angular velocity: ω = k * c (relates wave number and phase speed)
		// User speed multiplier allows artistic control while maintaining physical relationships
		waves[j].angularVelocity = waveNumberBase * phaseSpeed * speedMult * speedScale[j];
		
		// Steepness parameter controls wave sharpness (0 = sine wave, 1 = sharpest)
		// Sum of all steepness should not exceed 1 to prevent wave loops
		waves[j].steepness = saturate(baseSteepness[j] * steepnessMult * contributions[j]);
		
		waves[j].phaseOffset = dayPhase * dayScale[j] + dayBias[j];
	}

	float3 totalDisplacement = float3(0.0f, 0.0f, 0.0f);
	
	// Initialize tangent and binormal for normal calculation
	// Start with flat plane: tangent = (1, 0, 0), binormal = (0, 0, 1)
	float3 tangent = float3(1.0f, 0.0f, 0.0f);
	float3 binormal = float3(0.0f, 0.0f, 1.0f);

	[unroll] for (int k = 0; k < 3; ++k) {
		// Skip disabled waves entirely to prevent precision issues
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
	totalDisplacement.xy = clamp(totalDisplacement.xy, -100.0f, 100.0f);
	totalDisplacement.z = clamp(totalDisplacement.z, -50.0f, 50.0f);

	WaveSample sample;
	sample.displacement = totalDisplacement;
	sample.normal = waveNormal;
	sample.primaryDirection = primaryDir;
	sample.shoreInfluence = 0.0f;
	sample.shoreDistance = 10000.0f;
	return sample;
}

#endif // UNIFIED_WATER

#	ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
#		if !defined(VR)
	float4 QPosAdjust[1] : packoffset(c0);
#		else
	float4 QPosAdjust[2] : packoffset(c0);
#		endif  // VR
};

cbuffer PerMaterial : register(b1)
{
	float4 VSFogParam : packoffset(c0);
	float4 VSFogNearColor : packoffset(c1);
	float4 VSFogFarColor : packoffset(c2);
	float4 NormalsScroll0 : packoffset(c3);
	float4 NormalsScroll1 : packoffset(c4);
	float4 NormalsScale : packoffset(c5);
};

cbuffer PerGeometry : register(b2)
{
#		if !defined(VR)
	row_major float4x4 World[1] : packoffset(c0);
	row_major float4x4 PreviousWorld[1] : packoffset(c4);
	row_major float4x4 WorldViewProj[1] : packoffset(c8);
	float3 ObjectUV : packoffset(c12);
	float4 CellTexCoordOffset : packoffset(c13);
#		else   // VR has 25 vs 13 entries
	row_major float4x4 World[2] : packoffset(c0);
	row_major float4x4 PreviousWorld[2] : packoffset(c8);
	row_major float4x4 WorldViewProj[2] : packoffset(c16);
	float3 ObjectUV : packoffset(c24);
	float4 CellTexCoordOffset : packoffset(c25);
#		endif  // VR
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	uint eyeIndex = Stereo::GetEyeIndexVS(
#		if defined(VR)
		input.InstanceID
#		endif
	);
	vsout.NormalsScale = NormalsScale;
#	if defined(UNIFIED_WATER)
	vsout.UnifiedWaveInfo = 0.0.xxxx;
	vsout.UnifiedWaveNormal = float4(0.0f, 0.0f, 1.0f, 0.0f);
#	endif

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 worldPos;
	float4 worldViewPos;

#if defined(UNIFIED_WATER)
	float4 currentPosition = inputPosition;
	float4 previousPosition = inputPosition;
	float4 worldPosBase = mul(World[eyeIndex], inputPosition);
	float2 waveWorldPos = worldPosBase.xy + FrameBuffer::CameraPosAdjust[eyeIndex].xy;
	float2 waveWorldPosPrev = worldPosBase.xy + FrameBuffer::CameraPreviousPosAdjust[eyeIndex].xy;
	float2 flowBiasDirVS = float2(0.0f, 0.0f);
	float flowBiasWeightVS = 0.0f;
#	if defined(FLOWMAP)
	if ((ObjectUV.x > 0.0f) && (ObjectUV.y > 0.0f) && (ObjectUV.z > 0.0f)) {
		float2 dims = max(float2(ObjectUV.x, ObjectUV.y), float2(1.0f, 1.0f));
		float2 cellShift = float2(floor(ObjectUV.z * 0.5f), floor((ObjectUV.z - 1.0f) * 0.5f));
		float2 centerScaledUV = float2(0.5f, 0.5f) * ObjectUV.z - cellShift;
		float2 flowUV = (CellTexCoordOffset.xy + centerScaledUV) / dims;
		float4 flowSample = FlowMapTex.SampleLevel(FlowMapSampler, flowUV, 0.0f);
		float flowStrength = saturate(flowSample.z * flowSample.w);
		float2 rawFlow = -(flowSample.xy * 2.0f - 1.0f);
		float flowLenSq = dot(rawFlow, rawFlow);
		if (flowStrength > 0.001f && flowLenSq > 1e-5f) {
			flowBiasDirVS = rawFlow * rsqrt(flowLenSq);
			flowBiasWeightVS = flowStrength;
		}
	}
#	endif
	float waveTimeSeconds = ComputeWaveTimeSeconds(GameTimeHours, RealTimeSeconds);
	float waveDayPhase = ComputeWaveDayPhase(GameTimeHours);
	WaveSample currentWave = CalculateWaterDisplacement(waveWorldPos, float2(0.0f, 0.0f), float2(0.0f, 0.0f), WaveIntensity, WaveAmplitude, WaveSpeed, WaveSteepness, waveTimeSeconds, waveDayPhase, flowBiasDirVS, flowBiasWeightVS, false);
	float3 waveDisplacement = currentWave.displacement;
	float horizontalDisplacement = length(waveDisplacement.xy);
	vsout.UnifiedWaveInfo = float4(currentWave.primaryDirection, waveDisplacement.z, currentWave.shoreInfluence);
	
	// Use properly calculated Gerstner wave normal instead of flat normal
	// This gives correct lighting and surface appearance with sharper crests and flatter troughs
	vsout.UnifiedWaveNormal = float4(currentWave.normal, horizontalDisplacement);
	const float2 fallbackWaveDirVS = float2(-0.70710678f, 0.70710678f);
	float2 wavePrimaryDirVS = currentWave.primaryDirection;
	float wavePrimaryLenSqVS = dot(wavePrimaryDirVS, wavePrimaryDirVS);
	float2 normalizedWaveDirVS = wavePrimaryLenSqVS > 1e-5f ? wavePrimaryDirVS * rsqrt(wavePrimaryLenSqVS) : fallbackWaveDirVS;
	float spreadAngleVS = 0.42f;
	float sinSpreadVS, cosSpreadVS;
	sincos(spreadAngleVS, sinSpreadVS, cosSpreadVS);
	float sinSpreadVSNeg, cosSpreadVSNeg;
	sincos(-spreadAngleVS, sinSpreadVSNeg, cosSpreadVSNeg);
	float2 waveDirForwardVS = float2(
		normalizedWaveDirVS.x * cosSpreadVS - normalizedWaveDirVS.y * sinSpreadVS,
		normalizedWaveDirVS.x * sinSpreadVS + normalizedWaveDirVS.y * cosSpreadVS);
	float2 waveDirBackwardVS = float2(
		normalizedWaveDirVS.x * cosSpreadVSNeg - normalizedWaveDirVS.y * sinSpreadVSNeg,
		normalizedWaveDirVS.x * sinSpreadVSNeg + normalizedWaveDirVS.y * cosSpreadVSNeg);
	
	// Disable wave direction alignment to prevent swirl distortion
	float alignStrengthVS = 0.0f;
	float alignSingleStrengthVS = 0.0f;
	
	currentPosition.xyz += waveDisplacement;

	float waveTimeSecondsPrev = ComputeWaveTimeSeconds(PrevGameTimeHours, PrevRealTimeSeconds);
	float waveDayPhasePrev = ComputeWaveDayPhase(PrevGameTimeHours);
	WaveSample prevWave = CalculateWaterDisplacement(waveWorldPosPrev, float2(0.0f, 0.0f), float2(0.0f, 0.0f), WaveIntensity, WaveAmplitude, WaveSpeed, WaveSteepness, waveTimeSecondsPrev, waveDayPhasePrev, flowBiasDirVS, flowBiasWeightVS, true);
	float3 prevWaveDisplacement = prevWave.displacement;
	previousPosition.xyz += prevWaveDisplacement;

	inputPosition = currentPosition;
	worldPos = mul(World[eyeIndex], currentPosition);
	worldViewPos = mul(WorldViewProj[eyeIndex], currentPosition);
#else
	worldPos = mul(World[eyeIndex], inputPosition);
	worldViewPos = mul(WorldViewProj[eyeIndex], inputPosition);
#endif
// #endif
#if defined(UNIFIED_WATER)
	// Don't modify depth with wave displacement - use true projected depth
	vsout.HPosition = worldViewPos;
#else
	float heightMult = min((1.0 / 10000.0) * max(worldViewPos.z - 70000, 0), 1);
	vsout.HPosition.xy = worldViewPos.xy;
	vsout.HPosition.z = heightMult * 0.5 + worldViewPos.z;
	vsout.HPosition.w = worldViewPos.w;
#endif

#		if defined(STENCIL)
	vsout.WorldPosition = worldPos;
	#if defined(UNIFIED_WATER)
	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], previousPosition);
	#else
	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], inputPosition);
	#endif
#		else

#		if !defined(UNIFIED_WATER)
	float fogDistanceFactor = min(VSFogFarColor.w, pow(saturate(length(worldViewPos.xyz) * VSFogParam.y - VSFogParam.x), NormalsScale.w));
	vsout.FogParam.xyz = lerp(VSFogNearColor.xyz, VSFogFarColor.xyz, fogDistanceFactor);
	vsout.FogParam.w = fogDistanceFactor;
		#endif
	
#if defined(UNIFIED_WATER)
	vsout.WPosition.xyz = worldPos.xyz;
	vsout.WPosition.w = length(worldPos.xyz);
#else
	vsout.WPosition.xyz = worldPos.xyz;
	vsout.WPosition.w = length(worldPos.xyz);
#endif

#			if defined(LOD)
	float4 posAdjust =
		ObjectUV.x ? 0.0 : (QPosAdjust[eyeIndex].xyxy + worldPos.xyxy) / NormalsScale.xxyy;

#				if defined(UNIFIED_WATER)
	// Align LOD scroll directions with Gerstner motion
	float4 baseScroll = 0.0;
	float2 vanillaDir1 = normalize(NormalsScroll0.xy + float2(0.001f, 0.001f));
	float vanillaMag1 = length(NormalsScroll0.xy);
	float2 vanillaDir2 = normalize(NormalsScroll0.zw + float2(0.001f, 0.001f));
	float vanillaMag2 = length(NormalsScroll0.zw);
	float2 alignedDir1 = normalize(lerp(vanillaDir1, waveDirForwardVS, alignStrengthVS));
	float2 alignedDir2 = normalize(lerp(vanillaDir2, waveDirBackwardVS, alignStrengthVS));
	baseScroll.xy = alignedDir1 * vanillaMag1 + posAdjust.xy;
	baseScroll.zw = alignedDir2 * vanillaMag2 + posAdjust.zw;
	vsout.TexCoord1.xyzw = baseScroll;
#				else
	vsout.TexCoord1.xyzw = NormalsScroll0 + posAdjust;
#				endif
#			else
#				if !defined(SPECULAR) || (NUM_SPECULAR_LIGHTS == 0)
	vsout.MPosition.xyzw = inputPosition.xyzw;
#				endif

	float2 posAdjust = worldPos.xy + QPosAdjust[eyeIndex].xy;

	float2 scrollAdjust1 = posAdjust / NormalsScale.xx;
	float2 scrollAdjust2 = posAdjust / NormalsScale.yy;
	float2 scrollAdjust3 = posAdjust / NormalsScale.zz;

#						if defined(UNIFIED_WATER) && defined(NORMAL_TEXCOORD)
	float2 cellShift = float2(floor(ObjectUV.z * 0.5), floor((ObjectUV.z - 1.0) * 0.5));
	float2 scaledUV = input.TexCoord0.xy * ObjectUV.z - cellShift;
#						endif

#				if !(defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS) || defined(DEPTH) || NUM_SPECULAR_LIGHTS == 0))
#					if defined(NORMAL_TEXCOORD)
	float3 normalsScale = 0.001 * NormalsScale.xyz;
#						if defined(UNIFIED_WATER)
	if (ObjectUV.x) {
		scrollAdjust1 = scaledUV / normalsScale.xx;
		scrollAdjust2 = scaledUV / normalsScale.yy;
		scrollAdjust3 = scaledUV / normalsScale.zz;
	}
#						else
	if (ObjectUV.x) {
		scrollAdjust1 = input.TexCoord0.xy / normalsScale.xx;
		scrollAdjust2 = input.TexCoord0.xy / normalsScale.yy;
		scrollAdjust3 = input.TexCoord0.xy / normalsScale.zz;
	}
#						endif
#					else
	if (ObjectUV.x) {
		scrollAdjust1 = 0.0;
		scrollAdjust2 = 0.0;
		scrollAdjust3 = 0.0;
	}
#					endif
#				endif

	vsout.TexCoord1 = 0.0;
	vsout.TexCoord2 = 0.0;
#				if defined(FLOWMAP)
#					if !(((defined(SPECULAR) || NUM_SPECULAR_LIGHTS == 0) || (defined(UNDERWATER) && defined(REFRACTIONS))) && !defined(NORMAL_TEXCOORD))
#						if defined(BLEND_NORMALS)
#							if defined(UNIFIED_WATER)
	float2 baseScroll1 = NormalsScroll0.xy + scrollAdjust1;
	float2 baseScroll2 = NormalsScroll0.zw + scrollAdjust2;
	float2 baseScroll3 = NormalsScroll1.xy + scrollAdjust3;

	float2 vanillaDir1 = normalize(NormalsScroll0.xy + float2(0.001f, 0.001f));
	float vanillaMag1 = length(NormalsScroll0.xy);
	float2 vanillaDir2 = normalize(NormalsScroll0.zw + float2(0.001f, 0.001f));
	float vanillaMag2 = length(NormalsScroll0.zw);
	float2 vanillaDir3 = normalize(NormalsScroll1.xy + float2(0.001f, 0.001f));
	float vanillaMag3 = length(NormalsScroll1.xy);
	float2 alignedDir1 = normalize(lerp(vanillaDir1, normalizedWaveDirVS, alignStrengthVS));
	float2 alignedDir2 = normalize(lerp(vanillaDir2, waveDirForwardVS, alignStrengthVS));
	float2 alignedDir3 = normalize(lerp(vanillaDir3, waveDirBackwardVS, alignStrengthVS));
	baseScroll1 = alignedDir1 * vanillaMag1 + scrollAdjust1;
	baseScroll2 = alignedDir2 * vanillaMag2 + scrollAdjust2;
	baseScroll3 = alignedDir3 * vanillaMag3 + scrollAdjust3;

	vsout.TexCoord1.xy = baseScroll1;
	vsout.TexCoord1.zw = baseScroll2;
	vsout.TexCoord2.xy = baseScroll3;
#							else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
#							endif
#						else
#							if defined(UNIFIED_WATER)
	float2 baseScroll1 = NormalsScroll0.xy + scrollAdjust1;

	float2 vanillaDir1 = normalize(NormalsScroll0.xy + float2(0.001f, 0.001f));
	float vanillaMag1 = length(NormalsScroll0.xy);
	float2 alignedDir1 = normalize(lerp(vanillaDir1, normalizedWaveDirVS, alignSingleStrengthVS));
	baseScroll1 = alignedDir1 * vanillaMag1 + scrollAdjust1;

	vsout.TexCoord1.xy = baseScroll1;
#							else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
#							endif
	vsout.TexCoord1.zw = 0.0;
	vsout.TexCoord2.xy = 0.0;
#						endif
#					endif
#					if !defined(NORMAL_TEXCOORD)
	vsout.TexCoord3 = 0.0;
#					elif defined(WADING)
#						if defined(UNIFIED_WATER)
	float2 wadingUV = (input.TexCoord0.xy - 0.5f) * 0.5f;
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + wadingUV) / ObjectUV.xy; 
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + wadingUV;
#						else
	vsout.TexCoord2.zw = ((-0.5 + input.TexCoord0.xy) * 0.1 + CellTexCoordOffset.xy) +
	                     float2(CellTexCoordOffset.z, -CellTexCoordOffset.w + ObjectUV.x) / ObjectUV.xx;
	vsout.TexCoord3.xy = -0.25 + (input.TexCoord0.xy * 0.5 + ObjectUV.yz);
#						endif
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#					elif (defined(REFRACTIONS) || NUM_SPECULAR_LIGHTS == 0 || defined(BLEND_NORMALS))
#						if defined(UNIFIED_WATER)
	float2 dims = float2(ObjectUV.x, ObjectUV.y);
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + scaledUV) / dims;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + scaledUV;
	vsout.TexCoord3.zw = scaledUV;
#						else
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + input.TexCoord0.xy) / ObjectUV.xx;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + input.TexCoord0.xy;
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#						endif
#					endif
	vsout.TexCoord4 = ObjectUV.xy;
#				else
#					if defined(UNIFIED_WATER)
	float2 baseScroll1 = NormalsScroll0.xy + scrollAdjust1;
	float2 baseScroll2 = NormalsScroll0.zw + scrollAdjust2;
	float2 baseScroll3 = NormalsScroll1.xy + scrollAdjust3;

	float2 vanillaDir1 = normalize(NormalsScroll0.xy + float2(0.001f, 0.001f));
	float vanillaMag1 = length(NormalsScroll0.xy);
	float2 vanillaDir2 = normalize(NormalsScroll0.zw + float2(0.001f, 0.001f));
	float vanillaMag2 = length(NormalsScroll0.zw);
	float2 vanillaDir3 = normalize(NormalsScroll1.xy + float2(0.001f, 0.001f));
	float vanillaMag3 = length(NormalsScroll1.xy);
	float2 alignedDir1 = normalize(lerp(vanillaDir1, normalizedWaveDirVS, alignStrengthVS));
	float2 alignedDir2 = normalize(lerp(vanillaDir2, waveDirForwardVS, alignStrengthVS));
	float2 alignedDir3 = normalize(lerp(vanillaDir3, waveDirBackwardVS, alignStrengthVS));
	baseScroll1 = alignedDir1 * vanillaMag1 + scrollAdjust1;
	baseScroll2 = alignedDir2 * vanillaMag2 + scrollAdjust2;
	baseScroll3 = alignedDir3 * vanillaMag3 + scrollAdjust3;

	vsout.TexCoord1.xy = baseScroll1;
	vsout.TexCoord1.zw = baseScroll2;
	vsout.TexCoord2.xy = baseScroll3;
#					else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
#					endif
	vsout.TexCoord2.z = worldViewPos.w;
	vsout.TexCoord2.w = 0;
#					if (defined(WADING) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)))
	vsout.TexCoord3 = 0.0;
#						if (defined(NORMAL_TEXCOORD) && ((!defined(BLEND_NORMALS) && !defined(VERTEX_ALPHA_DEPTH)) || defined(WADING)))
	vsout.TexCoord3.xy = input.TexCoord0;
#						endif
#						if defined(VERTEX_ALPHA_DEPTH) && defined(VC)
	vsout.TexCoord3.z = input.Color.w;
#						endif
#					endif
#				endif
#			endif
#		endif

#		ifdef VR
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(vsout.HPosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#		endif  // VR
	return vsout;
}

#	endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
#	if defined(UNDERWATER) || defined(SIMPLE) || defined(LOD) || defined(SPECULAR)
	float4 Lighting : SV_Target0;
#	endif

#	if defined(STENCIL)
	float4 WaterMask : SV_Target0;
	float2 MotionVector : SV_Target1;
#	endif
};

#	ifdef PSHADER

SamplerState ReflectionSampler : register(s0);
SamplerState RefractionSampler : register(s1);
SamplerState DisplacementSampler : register(s2);
SamplerState CubeMapSampler : register(s3);
SamplerState Normals01Sampler : register(s4);
SamplerState Normals02Sampler : register(s5);
SamplerState Normals03Sampler : register(s6);
SamplerState DepthSampler : register(s7);
SamplerState FlowMapNormalsSampler : register(s9);
SamplerState SSRReflectionSampler : register(s10);
SamplerState RawSSRReflectionSampler : register(s11);

Texture2D<float4> ReflectionTex : register(t0);
Texture2D<float4> RefractionTex : register(t1);
Texture2D<float4> DisplacementTex : register(t2);
TextureCube<float4> CubeMapTex : register(t3);
Texture2D<float4> Normals01Tex : register(t4);
Texture2D<float4> Normals02Tex : register(t5);
Texture2D<float4> Normals03Tex : register(t6);
Texture2D<float4> DepthTex : register(t7);
Texture2D<float4> FlowMapNormalsTex : register(t9);
Texture2D<float4> SSRReflectionTex : register(t10);
Texture2D<float4> RawSSRReflectionTex : register(t11);

cbuffer PerTechnique : register(b0)
{
#		if !defined(VR)
	float4 VPOSOffset : packoffset(c0);    // inverse main render target width and height in xy, 0 in zw
	float4 PosAdjust[1] : packoffset(c1);  // inverse framebuffer range in w
	float4 CameraDataWater : packoffset(c2);
	float4 SunDir : packoffset(c3);
	float4 SunColor : packoffset(c4);
#		else
	float4 VPOSOffset : packoffset(c0);    // inverse main render target width and height in xy, 0 in zw
	float4 PosAdjust[2] : packoffset(c1);  // inverse framebuffer range in w
	float4 CameraDataWater : packoffset(c3);
	float4 SunDir : packoffset(c4);
	float4 SunColor : packoffset(c5);
#		endif
}

cbuffer PerMaterial : register(b1)
{
	float4 ShallowColor : packoffset(c0);
	float4 DeepColor : packoffset(c1);
	float4 ReflectionColor : packoffset(c2);
	float4 FresnelRI : packoffset(c3);    // Fresnel amount in x, specular power in z
	float4 BlendRadius : packoffset(c4);  // flowmap scale in y, specular radius in z
	float4 VarAmounts : packoffset(c5);   // Sun specular power in x, reflection amount in y, alpha in z, refraction magnitude in w
	float4 NormalsAmplitude : packoffset(c6);
	float4 WaterParams : packoffset(c7);   // noise falloff in x, reflection magnitude in y, sun sparkle power in z, framebuffer range in w
	float4 FogNearColor : packoffset(c8);  // above water fog amount in w
	float4 FogFarColor : packoffset(c9);
	float4 FogParam : packoffset(c10);      // above water fog distance far in z, above water fog range in w
	float4 DepthControl : packoffset(c11);  // depth reflections factor in x, depth refractions factor in y, depth normals factor in z, depth specular lighting factor in w
	float4 SSRParams : packoffset(c12);     // fWaterSSRIntensity in x, fWaterSSRBlurAmount in y, inverse main render target width and height in zw
	float4 SSRParams2 : packoffset(c13);    // fWaterSSRNormalPerturbationScale in x
}

cbuffer PerGeometry : register(b2)
{
#		if !defined(VR)
	float4x4 TextureProj[1] : packoffset(c0);
	float4 ReflectPlane[1] : packoffset(c4);
	float4 ProjData : packoffset(c5);
	float4 LightPos[8] : packoffset(c6);
	float4 LightColor[8] : packoffset(c14);
#		else
	float4x4 TextureProj[2] : packoffset(c0);
	float4 ReflectPlane[2] : packoffset(c8);
	float4 ProjData : packoffset(c10);
	float4 LightPos[8] : packoffset(c11);
	float4 LightColor[8] : packoffset(c19);
#		endif  //VR
}

#		if defined(VR)
/**
Calculates the depthMultiplier as used in Water.hlsl

VR appears to require use of CameraProjInverse and does not use ProjData
@param uv UV coords to convert
@param depth The calculated depth
@param eyeIndex The eyeIndex; 0 is left, 1 is right
@returns depthMultiplier
*/
float CalculateDepthMultFromUV(float2 uv, float depth, uint eyeIndex = 0)
{
	float4 temp;
	temp.xy = (uv * 2 - 1);
	temp.z = depth;
	temp.w = 1;
	temp = mul(FrameBuffer::CameraProjInverse[eyeIndex], temp.xyzw);
	temp.xyz /= temp.w;
	return length(temp.xyz);
}
#		endif  // VR

#		define SampColorSampler Normals01Sampler
#		define LinearSampler Normals01Sampler

#		if defined(TERRAIN_SHADOWS)
#			include "TerrainShadows/TerrainShadows.hlsli"
#		endif

#		if defined(SKYLIGHTING)
#			include "Skylighting/Skylighting.hlsli"
#		endif

#		if defined(CLOUD_SHADOWS)
#			include "CloudShadows/CloudShadows.hlsli"
#		endif

#		include "Common/ShadowSampling.hlsli"

#		if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
#			if defined(FLOWMAP)

/**
 * Structure containing complete flowmap information
 */
struct FlowmapData
{
	float4 color;       // Raw flowmap color (R=flow_x, G=flow_y, B=flow_strength, A=flow_mask)
	float2 flowVector;  // Flow vector (coordinate space depends on source function)
};

#			endif

#			if defined(FLOWMAP)
/**
 * Gets raw flowmap data before UV-space coordinate transformation
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for sampling the flowmap texture
 * @return FlowmapData with raw components:
 *         - color: Raw flowmap texture sample (RG=rotation, B=strength, A=mask)
 *         - flowVector: Base flow vector before any coordinate transformation
 *                      Ready for direct application of rotation matrix for world positioning
 *
 * @details This function provides flowmap data in its original coordinate space, suitable
 *          for world-space positioning effects (like ripple movement). The flowVector has
 *          NOT been transformed for UV-space normal sampling - that transformation is only
 *          applied in GetFlowmapDataUV() which uses transpose for UV coordinate perturbation.
 *
 *          Use this function when you need to apply the rotation matrix directly for
 *          world-space effects without needing to reverse any existing transformations.
 *
 * @see GetFlowmapDataUV() for UV-space normal sampling (applies transpose transformation)
 */
FlowmapData GetFlowmapDataTextureSpace(PS_INPUT input, float2 uvShift)
{
	FlowmapData data;
	data.color = FlowMapTex.Sample(FlowMapSampler, input.TexCoord2.zw + uvShift);
	data.flowVector = (64 * input.TexCoord3.xy) * sqrt(1.01 - data.color.z);
	// NOTE: flowVector is NOT transformed yet - this is the raw vector before rotation matrix
	return data;
}
/**
 * Samples flowmap texture and calculates UV-space flow data for texture sampling
 *
 * @param input Pixel shader input containing texture coordinates and world position data
 * @param uvShift UV offset for sampling the flowmap texture (used for animation/variation)
 * @return FlowmapData Complete flowmap information with UV-space flow vector
 *
 * @details This function:
 *          - Samples the flowmap texture at the specified UV coordinates
 *          - Decodes flow direction from RG channels (remapped from [0,1] to [-1,1])
 *          - Calculates flow strength using the blue channel with sqrt falloff
 *          - Applies transpose rotation matrix to transform flow direction to UV space
 *          - Scales flow vector by world position and strength factors
 *
 * @note Flowmap format:
 *       - Red channel: Flow direction X component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Green channel: Flow direction Y component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Blue channel: Flow strength (0 = no flow, 1 = maximum flow)
 *       - Alpha channel: Flow mask/intensity multiplier
 */
FlowmapData GetFlowmapDataUV(PS_INPUT input, float2 uvShift)
{
	FlowmapData data = GetFlowmapDataTextureSpace(input, uvShift);
	float2 flowSinCos = data.color.xy * 2 - 1;
	float2x2 flowRotationMatrix = float2x2(flowSinCos.x, flowSinCos.y, -flowSinCos.y, flowSinCos.x);
	data.flowVector = mul(transpose(flowRotationMatrix), data.flowVector);
	return data;
}

/**
 * Generates flowmap-based normal perturbation for water surface
 *
 * @param input Pixel shader input containing texture coordinates and world position
 * @param uvShift UV offset for flowmap sampling (used for animation phases)
 * @param multiplier Intensity multiplier for the flow effect
 * @param offset Base UV offset for the normal texture sampling
 * @return float3 Normal perturbation (XY=normal offset, Z=flow strength mask)
 *
 * @details This function uses flowmap data to:
 *          - Calculate flow-displaced UV coordinates for normal texture sampling
 *          - Apply flow-based animation to water normal textures
 *          - Return both the normal perturbation and flow strength information
 *
 * @note The returned Z component contains the original flowmap strength value
 *       which can be used for blending between flow and non-flow normals
 */
float3 GetFlowmapNormal(PS_INPUT input, float2 uvShift, float multiplier, float offset)
{
	FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
	float2 uv = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));
	return float3(FlowMapNormalsTex.SampleBias(FlowMapNormalsSampler, uv, SharedData::MipBias).xy, flowData.color.z);
}

/**
 * Gets flowmap data with world-space flow vector for positioning effects
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for flowmap sampling (used for animation phases)
 * @return FlowmapData Complete flowmap information with world-space flow vector
 *
 * @details This function:
 *          - Samples raw flowmap data (before UV-space transformations)
 *          - Decodes flow direction from flowmap RG channels
 *          - Applies component-wise directional transformation
 *          - Returns complete flowmap data with world-space flow vector
 *
 * @note Use this for effects that need to move with water current (ripples, debris, foam, etc.)
 *       For UV-space normal sampling, use GetFlowmapDataUV() instead
 */
FlowmapData GetFlowmapDataWorldSpace(PS_INPUT input, float2 uvShift)
{
	FlowmapData data = GetFlowmapDataTextureSpace(input, uvShift);
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}

/**
 * Converts existing texture-space flowmap data to world-space (avoids duplicate sampling)
 *
 * @param textureSpaceData FlowmapData from GetFlowmapDataTextureSpace()
 * @return FlowmapData Complete flowmap data with world-space flow vector
 *
 * @note Use this overload when you already have texture-space flowmap data to avoid duplicate texture sampling
 */
FlowmapData GetFlowmapDataWorldSpace(FlowmapData textureSpaceData)
{
	FlowmapData data = textureSpaceData;
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}
#			endif

#			if defined(UNIFIED_WATER)
/**
 * Enhanced Wave System - Gerstner Wave Implementation
 * Based on GPU Gems Chapter 1 by Mark Finch and Cyan Worlds
 * Provides more realistic wave simulation with directionality and steepness control
 */

/**
 * Calculate Gerstner wave values for a given position
 * @param position 2D world position
 * @param direction Normalized wave direction vector
 * @param amplitude Wave height multiplier
 * @param wavelength Distance between wave peaks
 * @param steepness Wave steepness factor (0-1, higher = more peaked)
 * @param timer Time value for animation
 * @return float3 (cos(phase), sin(phase), frequency)
 */

float3 GerstnerWaveValues(float2 position, float2 direction, float amplitude, float wavelength, float steepness, float timer)
{
	float w = 2.0 * 3.14159265 / wavelength;
	float dotD = dot(position, direction);
	float phase = w * dotD + timer;
	return float3(cos(phase), sin(phase), w);
}

/**
 * Calculate Gerstner wave normal contribution
 * @param direction Wave direction
 * @param amplitude Wave amplitude  
 * @param steepness Wave steepness (Q factor)
 * @param vals Wave values from GerstnerWaveValues
 * @return Normal contribution
 */

float3 GerstnerWaveNormal(float2 direction, float amplitude, float steepness, float3 vals)
{
	float C = vals.x;
	float S = vals.y; 
	float w = vals.z;
	float WA = w * amplitude;
	float WAC = WA * C;
	float3 normal = float3(-direction.x * WAC, 1.0 - steepness * WA * S, -direction.y * WAC);
	return normalize(normal);
}

/**
 * Calculate Gerstner wave displacement
 * @param direction Wave direction
 * @param amplitude Wave amplitude
 * @param steepness Wave steepness
 * @param vals Wave values from GerstnerWaveValues
 * @return 3D displacement vector
 */

float3 GerstnerWaveDisplacement(float2 direction, float amplitude, float steepness, float3 vals)
{
	float C = vals.x;
	float S = vals.y;
	float Q = steepness / (2.0 * 3.14159265 / 60.0 * amplitude); // Normalize steepness
	return float3(Q * amplitude * direction.x * C, amplitude * S, Q * amplitude * direction.y * C);
}

/**
 * Compute enhanced wave normal with Gerstner wave contribution
 * @param worldPos World space position
 * @param baseNormal Existing normal from texture sampling
 * @param timer Time for animation
 * @param waveIntensity Overall wave intensity multiplier
 * @return Enhanced normal with wave displacement
 */

float3 ComputeEnhancedWaveNormal(float3 worldPos, float3 baseNormal, float timer, float waveIntensity)
{
	if (waveIntensity < 0.01) return baseNormal;
	
	float2 waveDir1 = normalize(float2(1.0, 0.3));
	float2 waveDir2 = normalize(float2(-0.5, 1.0));
	
	float3 combinedNormal = baseNormal;
	
	float3 vals1 = GerstnerWaveValues(worldPos.xz * 0.01, waveDir1, 0.8, 120.0, 0.3, timer * 0.5);
	float3 normal1 = GerstnerWaveNormal(waveDir1, 0.8, 0.3, vals1);
	
	float3 vals2 = GerstnerWaveValues(worldPos.xz * 0.3, waveDir2, 0.5, 4.0, 0.4, timer * 0.7);
	float3 normal2 = GerstnerWaveNormal(waveDir2, 0.5, 0.4, vals2);
	
	float3 vals3 = GerstnerWaveValues(worldPos.xz * 1.0, waveDir1, 0.3, 1.2, 0.6, timer * 1.2);
	float3 normal3 = GerstnerWaveNormal(waveDir1, 0.3, 0.6, vals3);
	
	combinedNormal = normalize(baseNormal + waveIntensity * (normal1 * 0.3 + normal2 * 0.2 + normal3 * 0.5));

	return combinedNormal;
}

#			endif

#			if defined(LOD)
#				undef WATER_EFFECTS
#				undef WETNESS_EFFECTS
#			endif

#			if defined(WATER_EFFECTS) && !defined(VC)
#				define WATER_PARALLAX
#				include "WaterEffects/WaterParallax.hlsli"
#			endif

#			if defined(WATER_PARALLAX) && defined(FLOWMAP)

// ===== FLOWMAP PARALLAX TOGGLE =====
// Comment out the next line to disable flowmap parallax
#define ENABLE_FLOWMAP_PARALLAX
// ===================================

/**
 * Flowmap normal sampling with parallax occlusion mapping
 * Uses height data from flowmap alpha channel for depth perception
 */
float3 GetFlowmapNormalParallax(PS_INPUT input, float2 uvShift, float multiplier, float offset, uint eyeIndex)
{
	FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
	
	// Calculate base UV with flow displacement (same as non-parallax version)
	float2 baseUV = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));
	
#ifdef ENABLE_FLOWMAP_PARALLAX
	// Calculate mip level from base UV BEFORE parallax to avoid derivative discontinuities
	float2 textureDims;
	FlowMapNormalsTex.GetDimensions(textureDims.x, textureDims.y);
	float2 dx = ddx(baseUV * textureDims);
	float2 dy = ddy(baseUV * textureDims);
	float delta = max(dot(dx, dx), dot(dy, dy));
	float mipLevel = 0.5 * log2(max(delta, 0.00001)) + SharedData::MipBias;
	mipLevel = clamp(mipLevel, 0.0, 5.0);
	
	// Apply parallax using gradient-based view direction (avoids player-position artifacts)
	float2 parallaxOffset = WaterEffects::GetFlowmapParallaxOffset(input, baseUV);
	float2 finalUV = baseUV + parallaxOffset;
	
	// Sample using pre-calculated mip level
	float3 normalSample = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, finalUV, mipLevel).xyz;
#else
	// No parallax - sample directly
	float3 normalSample = FlowMapNormalsTex.Sample(FlowMapNormalsSampler, baseUV).xyz;
#endif
	
	return float3(normalSample.xy, flowData.color.z);
}
#			endif

#			if defined(DYNAMIC_CUBEMAPS)
#				include "DynamicCubemaps/DynamicCubemaps.hlsli"
#			endif

#			if defined(WETNESS_EFFECTS)
#				include "WetnessEffects/WetnessEffects.hlsli"
#			endif

// Foam data structure for unified water
struct FoamData
{
	float density;      // Foam coverage (0-1)
	float3 color;       // Foam color with lighting
	float roughness;    // Surface roughness for BRDF
	float thickness;    // For SSS calculation
};

#			if defined(UNIFIED_WATER)
/**
 * Physically-Based Foam System with BRDF and SSS
 * High-resolution procedural foam using Perlin noise independent of wave crests
 * Includes specular highlights (GGX BRDF), subsurface scattering, and light blue coloration
 */
FoamData ComputePhysicalFoam(float3 worldPos, float waterDepth, float timer, float foamIntensityMult, float2 screenPos, float3 normal, float3 viewDir, float3 lightDir, float4 waveInfo, float4 waveNormalData)
{
	FoamData foam;
	foam.density = 0.0f;
	foam.color = float3(0.85f, 0.95f, 1.0f); // Light blue base color
	foam.roughness = 0.35f;
	foam.thickness = 0.08f;
	
	if (foamIntensityMult <= 0.001f)
		return foam;

	float2 absoluteWorldPos = worldPos.xz;

	// Extract wave properties
	float2 primaryDir = waveInfo.xy;
	float primaryDirLen = dot(primaryDir, primaryDir);
	float2 flowDir = primaryDirLen > 1e-5f ? primaryDir * rsqrt(primaryDirLen) : float2(-0.70710678f, 0.70710678f);
	float2 flowPerp = float2(-flowDir.y, flowDir.x);
	
	// Enhanced wave displacement analysis
	float waveHeight = waveInfo.z;  // Actual vertical displacement from Gerstner waves
	float shoreInfluence = waveInfo.w;  // Shore proximity from vertex shader
	float horizontalDisplacement = waveNormalData.w;  // Lateral wave motion
	
	// Enhanced shallow water detection
	// Combine depth-based and shore influence for better coastal behavior
	float depthFactor = saturate((256.0f - max(waterDepth, 0.0f)) / 256.0f);
	float shallowBoost = saturate(depthFactor + shoreInfluence * 0.5f);
	shallowBoost = pow(shallowBoost, 1.5f);  // Sharper falloff from shore

	// Analyze wave crest characteristics
	float waveNormalLenSq = dot(waveNormalData.xyz, waveNormalData.xyz);
	float3 crestNormal = waveNormalLenSq > 1e-5f ? waveNormalData.xyz * rsqrt(waveNormalLenSq) : float3(0.0f, 0.0f, 1.0f);
	
	// Crest sharpness - steeper normals = sharper crests = more foam
	float crestSharpness = saturate(1.0f - crestNormal.z);
	
	// Wave amplitude and height-based crest detection
	float amplitudeScale = max(WaveAmplitude, 0.0001f);
	
	// Detect wave peaks: foam accumulates at the TOP of waves
	// Use actual wave height displacement, not just normalized position
	float waveHeightNormalized = waveHeight / (amplitudeScale + 0.0001f);
	float crestHeight = saturate((waveHeightNormalized + 0.5f) * 1.2f);  // Positive displacement = crest
	crestHeight = pow(crestHeight, 2.0f);  // Sharper peak detection
	
	// Lateral motion creates turbulence and foam
	float lateralMotion = saturate(horizontalDisplacement / (amplitudeScale * 6.0f + 8.0f));
	
	// Wave steepness indicator - combine multiple factors
	// Steep waves break and create whitecaps
	float waveSpeed = length(primaryDir) / max(amplitudeScale, 0.01f);
	float steepnessFactor = saturate(waveSpeed * 0.3f);
	
	// Whitecap energy: combination of height, steepness, and motion
	// This is the primary driver for foam on wave crests
	float whitecapEnergy = saturate(
		crestSharpness * 1.2f +      // Steep slope creates foam
		crestHeight * 1.5f +          // Peak of wave has most foam
		lateralMotion * 0.9f +        // Horizontal motion adds turbulence
		steepnessFactor * 0.4f        // Fast steep waves break
	);
	whitecapEnergy = pow(whitecapEnergy, 1.3f);  // Non-linear accumulation
	
	// Turbulence: wave breaking and chaotic motion
	float turbulence = saturate(
		crestSharpness * 0.8f + 
		lateralMotion * 0.6f +
		shallowBoost * 0.3f  // Shallow water increases turbulence
	);

	// Base flow with spatial variation to prevent uniform patterns
	// Flow speed increases with wave energy and shallow water turbulence
	float flowSpeed = FoamFlowSpeedBase + FoamFlowSpeedRange * (whitecapEnergy * 0.7f + shallowBoost * 0.3f);
	
	// Add spatial hash to break up uniform flow direction
	float spatialVariation = frac(sin(dot(absoluteWorldPos * 0.0001f, float2(12.9898, 78.233))) * 43758.5453);
	float2 flowVariation = float2(
		sin(spatialVariation * 6.2832) * 0.3,
		cos(spatialVariation * 6.2832) * 0.3
	);
	
	// Advection with wave-driven flow
	// Near crests, foam flows faster and along wave direction
	float waveFlowBoost = crestHeight * lateralMotion * 15.0f;
	float2 advectedPos = absoluteWorldPos - (flowDir + flowVariation) * timer * (flowSpeed * 28.0f + waveFlowBoost);
	
	// Multi-frequency swirl using Perlin noise instead of simple dot product
	// This creates organic, flowing patterns instead of straight lines
	// Swirl is stronger at wave crests where water is more turbulent
	float swirlNoise1 = Random::perlinNoise(float3(absoluteWorldPos * 0.0008f, timer * 0.15f), 0x19u);
	float swirlNoise2 = Random::perlinNoise(float3(absoluteWorldPos * 0.0021f, timer * 0.25f), 0x2Fu);
	float swirlPhase = (swirlNoise1 + swirlNoise2 * 0.5f) * 6.2832; // Convert to radians
	
	float swirlAmplitude = (FoamSwirlStrength + FoamSwirlEnergyScale * whitecapEnergy) * (1.0f + turbulence * 0.5f);
	float swirlAmount = sin(swirlPhase) * swirlAmplitude;
	
	float jitter = Random::perlinNoise(float3(absoluteWorldPos * 0.002f, timer * 0.2f), 0x15u) * 2.0f - 1.0f;
	float jitterScale = FoamSwirlStrength * 0.5f + 1.0f;
	advectedPos += flowPerp * (swirlAmount + jitter * jitterScale);

	// Multi-scale foam pattern with wave-height based modulation
	float2 foamUV1 = advectedPos * 0.06f;
	float2 foamUV2 = advectedPos * 0.14f;
	float2 foamUV3 = advectedPos * 0.32f + flowDir * timer * 1.2f;

	float noise1 = Random::perlinNoise(float3(foamUV1, timer * 0.22f), 0x31u) * 0.5f + 0.5f;
	float noise2 = Random::perlinNoise(float3(foamUV2, timer * 0.31f), 0x53u) * 0.5f + 0.5f;
	float noise3 = Random::perlinNoise(float3(foamUV3, timer * 0.47f), 0x7Du) * 0.5f + 0.5f;

	// Combine noise layers with turbulence modulation
	float foamPattern = noise1 * 0.45f + noise2 * 0.35f + noise3 * 0.20f;
	foamPattern = lerp(foamPattern, turbulence, 0.25f);  // Increased turbulence influence
	
	// Add dither for smooth transitions
	float dither = Random::InterleavedGradientNoise(screenPos, SharedData::FrameCount);
	foamPattern = lerp(foamPattern, dither, 0.08f);
	
	// Wave crest foam: accumulates at peaks, enhanced by whitecap energy
	// Lower threshold for earlier foam appearance on crests
	float crestFoamBase = smoothstep(0.65f, 0.92f, foamPattern + whitecapEnergy * 0.35f) * whitecapEnergy;
	crestFoamBase = pow(crestFoamBase, 0.9f);  // Softer curve for more gradual buildup
	
	// Shoreline foam: enhanced in shallow water and near beaches
	// More aggressive threshold for concentrated beach foam
	float shallowFoamBase = smoothstep(0.68f, 0.94f, foamPattern + shallowBoost * 0.25f) * shallowBoost;
	shallowFoamBase *= lerp(0.25f, 0.65f, pow(shallowBoost, 2.0f));  // Stronger near shore
	
	// Turbulent foam: from wave breaking and chaotic motion
	float turbulentFoamBase = smoothstep(0.75f, 0.95f, foamPattern + turbulence * 0.15f) * turbulence * 0.25f;

	// Directional alignment: foam streaks along wave direction
	float2 surfaceXZ = normal.xz;
	float surfaceLen = length(surfaceXZ);
	float directionalFactor = surfaceLen > 1e-4f ? saturate(dot(surfaceXZ / surfaceLen, flowDir) * 0.5f + 0.5f) : 0.7f;
	
	// Apply strength multipliers
	float crestFoam = crestFoamBase * directionalFactor * FoamCrestStrength;
	float shallowFoam = shallowFoamBase * FoamShoreStrength;
	float turbulentFoam = turbulentFoamBase * FoamTurbulenceStrength;

	float combinedFoam = crestFoam + shallowFoam + turbulentFoam;
	combinedFoam *= lerp(0.52f, 1.04f, whitecapEnergy);

	float baseCoverage = saturate(combinedFoam * 1.18f);
	float intensityRange = clamp(foamIntensityMult, 0.0f, 2.0f);
	float limitedRange = min(intensityRange, 1.0f);
	float coverageScale = lerp(0.04f, 0.36f, limitedRange);
	float extraIntensity = max(intensityRange - 1.0f, 0.0f);
	float foamCoverage = saturate(baseCoverage * (coverageScale + extraIntensity * 0.3f));
	foam.density = foamCoverage;
	
	// GGX BRDF for specular highlights (using functions from SSGI)
	float3 H = normalize(lightDir + viewDir);
	float NdotH = saturate(dot(normal, H));
	float NdotV = saturate(dot(normal, viewDir));
	float NdotL = saturate(dot(normal, lightDir));
	float VdotH = saturate(dot(viewDir, H));
	
	float a = foam.roughness * foam.roughness;
	float a2 = a * a;
	float ggxDenom = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
	float D_GGX = a2 / (Math::PI * ggxDenom * ggxDenom);
	
	// Smith visibility term
	float visSmithV = NdotL * (NdotV * (1 - a) + a);
	float visSmithL = NdotV * (NdotL * (1 - a) + a);
	float visDenom = visSmithV + visSmithL;
	float Vis_Smith = (visDenom > 0) ? (0.5 / visDenom) : 0;
	
	// Schlick Fresnel
	float Fc = pow(1 - VdotH, 5);
	float F0 = 0.04f; // Water-foam interface
	float fresnel = Fc + (1 - Fc) * F0;
	
	float specular = D_GGX * Vis_Smith * fresnel;
	
	// Burley-inspired SSS approximation
	float3 sssScaling = float3(1.0f, 1.0f, 1.0f) / 3.5f; // Scaling factor
	float3 meanFreePath = float3(0.12f, 0.15f, 0.18f); // Mean free path (light blue tint)
	float sssRadius = foam.thickness;
	float3 sssR = sssRadius / meanFreePath;
	float3 negRbyD = -sssR / sssScaling;
	float3 sss = max((exp(negRbyD) + exp(negRbyD / 3.0f)) / (sssScaling * meanFreePath * 8.0f * Math::PI), 1e-12f);
	
	// Combine lighting contributions with much brighter base
	float3 diffuse = foam.color * max(NdotL, 0.4f); // Ensure minimum brightness
	float3 scattering = sss * foam.color * 0.3f;
	float3 specularColor = specular * 0.5f;
	foam.color = saturate(diffuse + scattering + specularColor);
	foam.color *= lerp(1.0f, 1.12f, whitecapEnergy);
	
	return foam;
}
#			endif

// Forward declaration to ensure availability across permutations
FoamData ComputePhysicalFoam(float3 worldPos, float waterDepth, float timer, float foamIntensityMult, float2 screenPos, float3 normal, float3 viewDir, float3 lightDir, float4 waveInfo, float4 waveNormalData);

// Structure to return both normal and ripple/splash color information
struct WaterNormalData
{
	float3 normal;
	float4 rippleInfo;  // xyz = scaled ripple normal (normalized normal * intensity), w = splash effect intensity
};

WaterNormalData GetWaterNormal(PS_INPUT input, float distanceFactor, float normalsDepthFactor, float3 viewDirection, float depth, uint eyeIndex)
{
	WaterNormalData result;
	result.rippleInfo = float4(0, 0, 0, 0);
	float3 normalScalesRcp = rcp(input.NormalsScale.xyz);

#			if defined(WATER_PARALLAX)
	float2 parallaxOffset = WaterEffects::GetParallaxOffset(input, normalScalesRcp);
#			endif

#			if defined(FLOWMAP)
#				if defined(UNIFIED_WATER)
	float2 flowmapDimensions = input.TexCoord4.xy;
#				else
	float2 flowmapDimensions = input.TexCoord4.xx;
#				endif
	float2 normalMul = 0.5 + -(-0.5 + abs(frac(input.TexCoord2.zw * (64 * flowmapDimensions)) * 2 - 1));
	float2 uvShift = 1 / (128 * flowmapDimensions);

#				if defined(WATER_PARALLAX)
	// Use parallax-enabled flowmap normals
	float3 flowmapNormal0 = GetFlowmapNormalParallax(input, uvShift, 9.92, 0, eyeIndex);
	float3 flowmapNormal1 = GetFlowmapNormalParallax(input, float2(0, uvShift.y), 10.64, 0.27, eyeIndex);
	float3 flowmapNormal2 = GetFlowmapNormalParallax(input, 0.0.xx, 8, 0, eyeIndex);
	float3 flowmapNormal3 = GetFlowmapNormalParallax(input, float2(uvShift.x, 0), 8.48, 0.62, eyeIndex);
#				else
	// Standard flowmap normals without parallax
	float3 flowmapNormal0 = GetFlowmapNormal(input, uvShift, 9.92, 0);
	float3 flowmapNormal1 = GetFlowmapNormal(input, float2(0, uvShift.y), 10.64, 0.27);
	float3 flowmapNormal2 = GetFlowmapNormal(input, 0.0.xx, 8, 0);
	float3 flowmapNormal3 = GetFlowmapNormal(input, float2(uvShift.x, 0), 8.48, 0.62);
#				endif

	float2 flowmapNormalWeighted =
		normalMul.y * (normalMul.x * flowmapNormal2.xy + (1 - normalMul.x) * flowmapNormal3.xy) +
		(1 - normalMul.y) *
			(normalMul.x * flowmapNormal1.xy + (1 - normalMul.x) * flowmapNormal0.xy);
	float2 flowmapDenominator = sqrt(normalMul * normalMul + (1 - normalMul) * (1 - normalMul));
	float3 flowmapNormal =
		float3(((-0.5 + flowmapNormalWeighted) / (flowmapDenominator.x * flowmapDenominator.y)) *
				   max(0.4, normalsDepthFactor),
			0);
	flowmapNormal.z =
		sqrt(1 - flowmapNormal.x * flowmapNormal.x - flowmapNormal.y * flowmapNormal.y);
#			endif

#			if defined(WATER_PARALLAX)
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, input.TexCoord1.xy + parallaxOffset.xy * normalScalesRcp.x, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#			else
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, input.TexCoord1.xy, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#			endif

#			if defined(FLOWMAP) && !defined(BLEND_NORMALS)
#				ifdef DISABLE_FLOWMAP_NORMALS
	// FLOWMAP NORMALS DISABLED: Using only base normals (flow system still active for ripples/splashes)
	float3 finalNormal = normalize(normals1 + float3(0, 0, 1));
#				else
	// FLOWMAP NORMALS ENABLED: Blending flow-based normals with base normals
	float3 finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), flowmapNormal, distanceFactor));
#				endif
#			elif !defined(LOD)

#				if defined(WATER_PARALLAX)
	float3 normals2 = Normals02Tex.SampleBias(Normals02Sampler, input.TexCoord1.zw + parallaxOffset.xy * normalScalesRcp.y, SharedData::MipBias).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.SampleBias(Normals03Sampler, input.TexCoord2.xy + parallaxOffset.xy * normalScalesRcp.z, SharedData::MipBias).xyz * 2.0 - 1.0;
#				else
	float3 normals2 = Normals02Tex.SampleBias(Normals02Sampler, input.TexCoord1.zw, SharedData::MipBias).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.SampleBias(Normals03Sampler, input.TexCoord2.xy, SharedData::MipBias).xyz * 2.0 - 1.0;
#				endif

	float3 blendedNormal = normalize(float3(0, 0, 1) + NormalsAmplitude.x * normals1 +
									 NormalsAmplitude.y * normals2 + NormalsAmplitude.z * normals3);
#				if defined(UNDERWATER)
	float3 finalNormal = blendedNormal;
#				else
	float3 finalNormal = normalize(lerp(float3(0, 0, 1), blendedNormal, normalsDepthFactor));
#				endif

#				if defined(FLOWMAP)
	float normalBlendFactor =
		normalMul.y * ((1 - normalMul.x) * flowmapNormal3.z + normalMul.x * flowmapNormal2.z) +
		(1 - normalMul.y) * (normalMul.x * flowmapNormal1.z + (1 - normalMul.x) * flowmapNormal0.z);
	finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), normalize(lerp(finalNormal, flowmapNormal, normalBlendFactor)), distanceFactor));
#				endif
#			else
	float3 finalNormal =
		normalize(float3(0, 0, 1) + NormalsAmplitude.xxx * normals1);
#			endif

#			if defined(WADING)
#				if defined(FLOWMAP)
	float2 displacementUv = input.TexCoord3.zw;
#				else
	float2 displacementUv = input.TexCoord3.xy;
#				endif
	float3 displacement = normalize(float3(NormalsAmplitude.w * (-0.5 + DisplacementTex.Sample(DisplacementSampler, displacementUv).zw),
		0.04));
	finalNormal = lerp(displacement, finalNormal, displacement.z);
#			endif

#			if defined(WETNESS_EFFECTS)
	// Wetness Effects Debug System:
	// DEBUG_WETNESS_EFFECTS Color Legend:
	// - BRIGHT MAGENTA: Ripples, BRIGHT GREEN: Splashes, CYAN: Both effects
	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
#				if defined(SKYLIGHTING)
#					if defined(VR)
	float3 positionMSSkylight = input.WPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#					else
	float3 positionMSSkylight = input.WPosition.xyz;
#					endif
	sh2 skylightingSH = Skylighting::sample(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, Skylighting::stbn_vec3_2Dx1D_128x128x64, input.HPosition.xy, positionMSSkylight, float3(0, 0, 1));
	float skylighting = SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1));

	float wetnessOcclusion = inWorld ? pow(saturate(skylighting), 2) : 0;
#				else
	float wetnessOcclusion = inWorld;
#				endif

	float4 raindropInfo = float4(0, 0, 1, 0);
	float maxRainDropDistance = SharedData::wetnessEffectsSettings.RaindropFxRange * SharedData::wetnessEffectsSettings.RaindropFxRange * 3;
	float rainDropDistance = dot(input.WPosition, input.WPosition);
	float distanceFadeout = saturate((1 - saturate(rainDropDistance / maxRainDropDistance)) * 3);
	if (finalNormal.z > 0 && SharedData::wetnessEffectsSettings.Raining > 0.0f && SharedData::wetnessEffectsSettings.EnableRaindropFx &&
		(rainDropDistance < maxRainDropDistance) && wetnessOcclusion > 0.05) {
		float rippleStrengthModifier = (wetnessOcclusion * wetnessOcclusion) * distanceFadeout;
		float3 rippleWPosition = input.WPosition.xyz + finalNormal * 16;
#				if defined(WATER_PARALLAX)
		rippleWPosition.xy += parallaxOffset;
#				endif
#				if defined(FLOWMAP)
		// Flow-following ripple enhancement: Makes raindrops follow water current
		FlowmapData worldFlowData = GetFlowmapDataWorldSpace(input, float2(0, 0));

		// Calculate flow-aware ripple offset using centralized timing logic
		// Parameters: avgFlowmapMultiplier=9.26 (average of GetWaterNormal flowmap normal multipliers: 9.92, 10.64, 8, 8.48)
		// uvToWorldScale=0.125 (1/8 - relates to 64× texture coordinate scaling factor)
		float2 flowOffset = WetnessEffects::GetFlowAwareRippleOffset(
			worldFlowData.flowVector,
			worldFlowData.color.w,      // Flow strength from flowmap alpha
			0.001 * ReflectionColor.w,  // Reflection timing scale (matches GetFlowmapNormal)
			9.26,                       // Average flowmap normal multiplier
			0.125                       // UV-to-world scale factor (1/8)
		);

		rippleWPosition.xy += flowOffset;
#				endif
		raindropInfo = WetnessEffects::GetRainDrops(rippleWPosition + FrameBuffer::CameraPosAdjust[eyeIndex].xyz, SharedData::wetnessEffectsSettings.Time, finalNormal, rippleStrengthModifier);

		// Calculate ripple and splash color intensities
		float rippleIntensity = length(raindropInfo.xy) * rippleStrengthModifier;
		float splashIntensity = raindropInfo.w * distanceFadeout;

		// Store ripple and splash information for color effects
		result.rippleInfo.xyz = raindropInfo.xyz * rippleIntensity;
		result.rippleInfo.w = splashIntensity;
		

	}
	float3 rippleNormal = normalize(raindropInfo.xyz);
	finalNormal = WetnessEffects::ReorientNormal(rippleNormal, finalNormal);
#			endif

	result.normal = finalNormal;
	return result;
}

float3 GetWaterSpecularColor(PS_INPUT input, float3 normal, float3 viewDirection,
	float distanceFactor, float refractionsDepthFactor, uint eyeIndex = 0)
{
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Reflections) {
		float3 finalSsrReflectionColor = 0.0.xxx;
		float ssrFraction = 0.0;
		float3 reflectionColor = 0.0.xxx;
		float3 R = reflect(viewDirection, WaterParams.y * normal + float3(0, 0, 1 - WaterParams.y));

		if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Cubemap) {
#			if defined(DYNAMIC_CUBEMAPS)
#				if defined(SKYLIGHTING)

			float3 dynamicCubemap;
			if (SharedData::InInterior) {
				dynamicCubemap = DynamicCubemaps::EnvTexture.SampleLevel(CubeMapSampler, R, 0).xyz;
			} else {
#					if defined(VR)
				float3 positionMSSkylight = input.WPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#					else
				float3 positionMSSkylight = input.WPosition.xyz;
#					endif

				sh2 skylighting = Skylighting::sample(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, Skylighting::stbn_vec3_2Dx1D_128x128x64, input.HPosition.xy, positionMSSkylight, R);
				sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normal, -viewDirection, 0.0);

				float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
				skylightingSpecular = lerp(1.0, skylightingSpecular, Skylighting::getFadeOutFactor(input.WPosition.xyz));
				skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

				float3 specularIrradiance = 1;

				if (skylightingSpecular < 1.0) {
					specularIrradiance = DynamicCubemaps::EnvTexture.SampleLevel(CubeMapSampler, R, 0).xyz;
					specularIrradiance = Color::GammaToLinear(specularIrradiance);
				}

				float3 specularIrradianceReflections = 1.0;

				if (skylightingSpecular > 0.0) {
					specularIrradianceReflections = DynamicCubemaps::EnvReflectionsTexture.SampleLevel(CubeMapSampler, R, 0).xyz;
					specularIrradianceReflections = Color::GammaToLinear(specularIrradianceReflections);
				}

				dynamicCubemap = Color::LinearToGamma(lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular));
			}
#				else
			float3 dynamicCubemap = DynamicCubemaps::EnvReflectionsTexture.SampleLevel(CubeMapSampler, R, 0);
#				endif

#				if defined(VR)
			// Reflection cubemap is incorrect for interiors in VR, ignore it
			if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior || SharedData::HideSky)
				reflectionColor = dynamicCubemap.xyz;
			else
				reflectionColor = lerp(dynamicCubemap.xyz, CubeMapTex.SampleLevel(CubeMapSampler, R, 0).xyz, saturate(length(input.WPosition.xyz) / 1024.0));
#				else
			if (SharedData::HideSky)
				reflectionColor = dynamicCubemap.xyz;
			else
				reflectionColor = lerp(dynamicCubemap.xyz, CubeMapTex.SampleLevel(CubeMapSampler, R, 0).xyz, saturate(length(input.WPosition.xyz) / 1024.0));
#				endif
#			else
			reflectionColor = CubeMapTex.SampleLevel(CubeMapSampler, R, 0).xyz;
#			endif
		} else {
#			if !defined(LOD) && NUM_SPECULAR_LIGHTS == 0
			float4 reflectionNormalRaw = float4((VarAmounts.w * refractionsDepthFactor) * normal.xy + input.MPosition.xy, input.MPosition.z, 1);
#			else
			float4 reflectionNormalRaw = float4(VarAmounts.w * normal.xy, 0, 1);
#			endif

			float4 reflectionNormal = mul(transpose(TextureProj[eyeIndex]), reflectionNormalRaw);
			reflectionColor = ReflectionTex.SampleLevel(ReflectionSampler, reflectionNormal.xy / reflectionNormal.ww, 0).xyz;
		}

#			if !defined(LOD) && NUM_SPECULAR_LIGHTS == 0
		if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Cubemap) {
			float pointingDirection = dot(viewDirection, R);
			float pointingAlignment = dot(reflect(viewDirection, float3(0, 0, 1)), R);
			float ssrAmount = min(pointingAlignment, pointingDirection);
			if (SSRParams.x > 0.0 && ssrAmount > 0.0) {
				float2 ssrReflectionUv = ((FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy) * SSRParams.zw) + 0.05 * normal.xy;
				float2 ssrReflectionUvDR = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(ssrReflectionUv);
				float4 ssrReflectionColorBlurred = RawSSRReflectionTex.Sample(RawSSRReflectionSampler, ssrReflectionUvDR);
				float4 ssrReflectionColorRaw = RawSSRReflectionTex.Sample(RawSSRReflectionSampler, ssrReflectionUvDR);
				float4 ssrReflectionColor = lerp(ssrReflectionColorBlurred, ssrReflectionColorRaw, ssrAmount * 0.7);

				finalSsrReflectionColor = max(0, ssrReflectionColor.xyz);
				ssrFraction = saturate(ssrReflectionColor.w * distanceFactor * ssrAmount);
			}
		}
#			endif

		float3 finalReflectionColor = Color::LinearToGamma(lerp(Color::GammaToLinear(reflectionColor), Color::GammaToLinear(finalSsrReflectionColor), ssrFraction));
		return finalReflectionColor;
	}
	return ReflectionColor.xyz * VarAmounts.y;
}

#if defined(DEPTH)
float GetScreenDepthWater(float2 screenPosition, uint a_useVR)
{
	float depth = DepthTex.Load(float3(screenPosition, 0)).x;
#			if defined(VR)  // VR appears to use hard coded values
	return depth * 1.01 + -0.01;
#			else
	return (CameraDataWater.w / (-depth * CameraDataWater.z + CameraDataWater.x));
#			endif
}
#endif


float3 GetLdotN(float3 normal)
{
#			if defined(UNDERWATER)
	return 1;
#			else
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
		return 1;
	return saturate(dot(SunDir.xyz, normal));
#			endif
}

float GetFresnelValue(float3 normal, float3 viewDirection)
{
#			if defined(UNDERWATER)
	float3 actualNormal = -normal;
#			else
	float3 actualNormal = normal;
#			endif
	float viewAngle = 1 - saturate(dot(-viewDirection, actualNormal));
	return (1 - FresnelRI.x) * pow(viewAngle, 5) + FresnelRI.x;
}

struct DiffuseOutput
{
	float3 refractionColor;
	float3 refractionDiffuseColor;
	float depth;
	float refractionMul;
};

DiffuseOutput GetWaterDiffuseColor(PS_INPUT input, float3 normal, float3 viewDirection, inout float4 distanceMul, float refractionsDepthFactor, float fresnel, uint eyeIndex, float3 viewPosition, float noise, float depth)
{
#			if defined(REFRACTIONS)
	float4 refractionNormal = mul(transpose(TextureProj[eyeIndex]), float4((VarAmounts.w * refractionsDepthFactor * normal.xy) + input.MPosition.xy, input.MPosition.z, 1));

	float2 refractionUvRaw = float2(refractionNormal.x, refractionNormal.w - refractionNormal.y) / refractionNormal.ww;
	refractionUvRaw = Stereo::ConvertToStereoUV(refractionUvRaw, eyeIndex);  // need to convert here for VR due to refractionNormal values

#				if defined(VR)
	float2 refractionUvRawNoStereo = Stereo::ConvertFromStereoUV(refractionUvRaw, eyeIndex, 1);
#				endif

	float2 screenPosition = FrameBuffer::DynamicResolutionParams1.xy * (FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);

	float2 refractionScreenPosition = FrameBuffer::DynamicResolutionParams1.xy * (refractionUvRaw / VPOSOffset.xy);
	float4 refractionWorldPosition = float4(input.WPosition.xyz * depth / viewPosition.z, 0);

#				if defined(DEPTH) && !defined(VERTEX_ALPHA_DEPTH)
	float refractionDepth = GetScreenDepthWater(refractionScreenPosition, eyeIndex);

#					if !defined(VR)
	float refractionDepthMul = length(float3((((VPOSOffset.zw + refractionUvRaw) * 2 - 1)) * refractionDepth / ProjData.xy, refractionDepth));
#					else
	float refractionDepthMul = CalculateDepthMultFromUV(refractionUvRawNoStereo, refractionDepth, eyeIndex);
#					endif  //VR

	float3 refractionDepthAdjustedViewDirection = -viewDirection * refractionDepthMul;
	float refractionViewSurfaceAngle = dot(refractionDepthAdjustedViewDirection, ReflectPlane[eyeIndex].xyz);

	float refractionPlaneMul = (1 - ReflectPlane[eyeIndex].w / refractionViewSurfaceAngle);

	if (refractionPlaneMul < 0.0) {
		refractionUvRaw = FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;  // This value is already stereo converted for VR
	} else {
		distanceMul = saturate(refractionPlaneMul * float4(length(refractionDepthAdjustedViewDirection).xx, abs(refractionViewSurfaceAngle).xx) / FogParam.z);

#					if defined(VR)
		refractionWorldPosition = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4((refractionUvRawNoStereo * 2 - 1), DepthTex.Load(float3(refractionScreenPosition, 0)).x, 1));
#					else
		refractionWorldPosition = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4((refractionUvRaw * 2 - 1) * float2(1, -1), DepthTex.Load(float3(refractionScreenPosition, 0)).x, 1));
#					endif
		refractionWorldPosition.xyz /= refractionWorldPosition.w;
	}
#				endif

	float2 refractionUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(refractionUvRaw);
	float3 refractionColor = RefractionTex.Sample(RefractionSampler, refractionUV).xyz;
	float3 refractionDiffuseColor = lerp(ShallowColor.xyz, DeepColor.xyz, distanceMul.y);

#				if defined(UNDERWATER)
	float refractionMul = 0;
#				else
	float refractionMul = 1 - pow(saturate((-distanceMul.x * FogParam.z + FogParam.z) / FogParam.w), FogNearColor.w);
#				endif

	DiffuseOutput output;
	output.refractionColor = refractionColor;
	output.refractionDiffuseColor = refractionDiffuseColor;
	output.depth = depth;
	output.refractionMul = refractionMul;
	return output;
#			else
	DiffuseOutput output;
	output.refractionColor = lerp(ShallowColor.xyz, DeepColor.xyz, fresnel) * GetLdotN(normal);
	output.refractionDiffuseColor = output.refractionColor;
	output.depth = 1;
	output.refractionMul = 1;
	return output;
#			endif
}

float3 GetSunColor(float3 normal, float3 viewDirection)
{
#			if defined(UNDERWATER)
	return 0.0.xxx;
#			else
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
		return 0.0.xxx;

	float3 reflectionDirection = reflect(viewDirection, normal);
	float reflectionMul = exp2(VarAmounts.x * log2(saturate(dot(reflectionDirection, SunDir.xyz))));

	return reflectionMul * SunColor.xyz * SunDir.w * DeepColor.w;
#			endif
}
#		endif

#		if defined(LIGHT_LIMIT_FIX)
#			include "LightLimitFix/LightLimitFix.hlsli"
#		endif

#		if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#			include "InverseSquareLighting/InverseSquareLighting.hlsli"
#		endif

#		if defined(IBL)
#			include "IBL/IBL.hlsli"
#		endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#ifdef FLOWMAP
	
	// float2 v = input.TexCoord2.zw;
	// float N  = 64.0;
	
	// float2 v = input.TexCoord3.xy;
	// float N  = 2.0;

	// float2 v = input.TexCoord3.zw;
	// float N  = 2.0;
	
	// float2 s = step(0.5, frac(v * N));
	// psout.Lighting = float4(s.x, s.y, 0, 1);
	// return psout;

	// psout.Lighting = float4(0.1, 0.12, 0.4, 1);
	// return psout;

#endif


	

	uint eyeIndex = Stereo::GetEyeIndexPS(input.HPosition, VPOSOffset);
	float2 screenPosition = FrameBuffer::DynamicResolutionParams1.xy * (FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);

#		if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
	float3 viewDirection = normalize(input.WPosition.xyz);

	float distanceFactor = saturate(lerp(FrameBuffer::FrameParams.w, 1, (input.WPosition.w - 8192) / (WaterParams.x - 8192)));
	float4 distanceMul = saturate(lerp(VarAmounts.z, 1, -(distanceFactor - 1))).xxxx;
	float distanceBlendFactor = distanceFactor;
#			if defined(UNIFIED_WATER)
	distanceBlendFactor = 1.0f;
#			endif

	bool isSpecular = false;

	float depth = 0;

#			if defined(DEPTH)
#				if defined(VERTEX_ALPHA_DEPTH)
#					if defined(VC)
	distanceMul = saturate(input.TexCoord3.z);
#					endif
#				else
	distanceMul = 0;

	depth = GetScreenDepthWater(screenPosition, eyeIndex);
	float2 depthOffset =
		FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;
#					if !defined(VR)
	float depthMul = length(float3((depthOffset * 2 - 1) * depth / ProjData.xy, depth));
#					else
	float depthMul = CalculateDepthMultFromUV(Stereo::ConvertFromStereoUV(depthOffset, eyeIndex, 1), depth, eyeIndex);
#					endif  //VR
	float3 depthAdjustedViewDirection = -viewDirection * depthMul;
	float viewSurfaceAngle = dot(depthAdjustedViewDirection, ReflectPlane[eyeIndex].xyz);

	float planeMul = (1 - ReflectPlane[eyeIndex].w / viewSurfaceAngle);
	distanceMul = saturate(
		planeMul * float4(length(depthAdjustedViewDirection).xx, abs(viewSurfaceAngle).xx) /
		FogParam.z);
#				endif
#			endif

#			if defined(UNDERWATER)
	float4 depthControl = float4(0, 1, 1, 0);
#			elif defined(LOD)
	float4 depthControl = float4(1, 0, 0, 1);
#			elif defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float4 depthControl = float4(0, 0, 1, 0);
#			else
	float4 depthControl = DepthControl * (distanceMul - 1) + 1;
#			endif
	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);

	WaterNormalData waterData = GetWaterNormal(input, distanceBlendFactor, depthControl.z, viewDirection, depth, eyeIndex);
	float3 normal = waterData.normal;

#		if defined(UNIFIED_WATER)
	FoamData foamData;
	foamData.density = 0.0f;
	foamData.color = float3(0.85f, 0.95f, 1.0f);
	float waterDepth = 1e5f; // Declare outside block so it's available later
	{
#		if defined(DEPTH)
		float surfaceDepth = -viewPosition.z;
		if (depth > 0.0f)
			waterDepth = max(0.0f, depth - surfaceDepth);
#		endif
		float waveTimeSeconds = ComputeWaveTimeSeconds(GameTimeHours, RealTimeSeconds);
		
		// Compute physically-based foam with BRDF and SSS
		// World position = camera offset + view-space position (same as used for lights)
		float3 worldPosForFoam = PosAdjust[eyeIndex].xyz + input.WPosition.xyz;
		float3 lightDir = normalize(-SunDir.xyz); // Sun direction
		foamData = ComputePhysicalFoam(worldPosForFoam, waterDepth, waveTimeSeconds, FoamIntensity, input.HPosition.xy, normal, viewDirection, lightDir, input.UnifiedWaveInfo, input.UnifiedWaveNormal);
		
		if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
			foamData.density *= 0.35f;
	}
#		if defined(UNDERWATER)
	foamData.density = 0.0f;
#		endif
#		endif

	float fresnel = GetFresnelValue(normal, viewDirection);

#			if defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float3 finalColor = 0.0.xxx;

	for (int lightIndex = 0; lightIndex < NUM_SPECULAR_LIGHTS; ++lightIndex) {
		float3 lightVector = LightPos[lightIndex].xyz - (PosAdjust[eyeIndex].xyz + input.WPosition.xyz);
		float3 lightDirection = normalize(normalize(lightVector) - viewDirection);
		float lightFade = saturate(length(lightVector) / LightPos[lightIndex].w);
		float lightColorMul = (1 - lightFade * lightFade);
		float LdotN = saturate(dot(lightDirection, normal));
		float3 lightColor = (LightColor[lightIndex].xyz * pow(LdotN, FresnelRI.z)) * lightColorMul;
		finalColor += lightColor;
	}

	finalColor *= fresnel;
#				if defined(WETNESS_EFFECTS) && defined(DEBUG_WETNESS_EFFECTS)
	// DEBUG MODE: Override specular color with debug visualization
	float3 debugColor = WetnessEffects::GetDebugWetnessColorSpecular(waterData.rippleInfo, 2.5, 4.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#				endif

	isSpecular = true;
#			else

	float shadow = 1;

	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	float3 specularColor = GetWaterSpecularColor(input, normal, viewDirection, distanceFactor, depthControl.y, eyeIndex);
	DiffuseOutput diffuseOutput = GetWaterDiffuseColor(input, normal, viewDirection, distanceMul, depthControl.y, fresnel, eyeIndex, viewPosition, screenNoise, depth);

	float3 diffuseColor = lerp(diffuseOutput.refractionColor, diffuseOutput.refractionDiffuseColor, diffuseOutput.refractionMul);

	depthControl = DepthControl * (distanceMul - 1) + 1;

	float3 specularLighting = 0;

#				if defined(LIGHT_LIMIT_FIX)
	uint lightCount = 0;

	uint clusterIndex = 0;
	if (LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
			LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];
			if (LightLimitFix::IsLightIgnored(light) || light.lightFlags & LightLimitFix::LightFlags::Shadow) {
				continue;
			}

			float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WPosition.xyz;
			float lightDist = length(lightDirection);

#					if defined(ISL)
			float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
#					else
			float intensityFactor = saturate(lightDist / light.radius);
			float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#					endif

			float3 normalizedLightDirection = normalize(lightDirection);

			float3 H = normalize(normalizedLightDirection - viewDirection);
			float HdotN = saturate(dot(H, normal));

			float3 lightColor = light.color.xyz * pow(HdotN, FresnelRI.z);
			specularLighting += lightColor * intensityMultiplier;
		}
	}
	specularColor += specularLighting * 3;
#				endif

#				if defined(UNDERWATER)
	float3 finalSpecularColor = lerp(ShallowColor.xyz, specularColor, 0.5);
	float3 finalColor = saturate(1 - input.WPosition.w * 0.002) * ((1 - fresnel) * (diffuseColor - finalSpecularColor)) + finalSpecularColor;
	// Add ripple and splash color effects for underwater
#					if defined(WETNESS_EFFECTS) && defined(DEBUG_WETNESS_EFFECTS)
	// DEBUG MODE: Override water color with debug visualization (darker for underwater)
	float3 debugColor = WetnessEffects::GetDebugWetnessColorUnderwater(waterData.rippleInfo, 1.5, 2.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#					endif
#				else
	float3 sunColor = GetSunColor(normal, viewDirection);

	if (!(Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior) && any(sunColor > 0.0)) {
		sunColor *= ShadowSampling::GetWaterShadow(screenNoise, input.WPosition.xyz, eyeIndex);
	}

#					if defined(VC)
	float specularFraction = lerp(1, fresnel * diffuseOutput.refractionMul, distanceBlendFactor);
	float3 finalColorPreFog = lerp(diffuseColor, specularColor, specularFraction) + sunColor * depthControl.w;
	
#						if !defined(UNIFIED_WATER)
	float fogDistanceFactor = input.FogParam.w;
	float3 fogColor = input.FogParam.xyz;
#						else
	float fogDistanceFactor = min(FogFarColor.w, pow(saturate(input.WPosition.w * FogParam.y - FogParam.x), FresnelRI.y));
	float3 fogColor = lerp(FogNearColor.xyz, FogFarColor.xyz, fogDistanceFactor);
#						endif
	
#						if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL && !SharedData::InInterior) {
		fogColor = ImageBasedLighting::GetFogIBLColor(fogColor);
	}
#						endif
	float3 finalColor = lerp(finalColorPreFog, fogColor * PosAdjust[eyeIndex].w, fogDistanceFactor);
#						if defined(WETNESS_EFFECTS) && defined(DEBUG_WETNESS_EFFECTS)
	// DEBUG MODE: Override water color with debug visualization
	float3 debugColor = WetnessEffects::GetDebugWetnessColorStandard(waterData.rippleInfo, 2.0, 3.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#						endif

#					else
	float specularFraction = lerp(1, fresnel, distanceBlendFactor);
	float3 finalColorPreFog = lerp(diffuseOutput.refractionDiffuseColor, specularColor, specularFraction) + sunColor * depthControl.w;

#						if !defined(UNIFIED_WATER)
	float fogDistanceFactor = input.FogParam.w;
	float3 preFogColor = input.FogParam.xyz;
#						else
	float fogDistanceFactor = min(FogFarColor.w, pow(saturate(input.WPosition.w * FogParam.y - FogParam.x), FresnelRI.y));
	float3 preFogColor = lerp(FogNearColor.xyz, FogFarColor.xyz, fogDistanceFactor);
#						endif
	
#						if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL && !SharedData::InInterior) {
		preFogColor = ImageBasedLighting::GetFogIBLColor(preFogColor);
	}
#						endif
	finalColorPreFog = lerp(finalColorPreFog, preFogColor * PosAdjust[eyeIndex].w, fogDistanceFactor);

	float3 refractionColor = diffuseOutput.refractionColor;

	float fogFactor = min(FogParam.w, pow(saturate(-diffuseOutput.depth * FogParam.y - FogParam.x), FogParam.z));
	float3 fogColor = lerp(FogNearColor.xyz, FogFarColor.xyz, fogFactor);
#						if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL && !SharedData::InInterior) {
		fogColor = ImageBasedLighting::GetFogIBLColor(fogColor);
	}
#						endif
	refractionColor = lerp(refractionColor, fogColor, fogFactor);

	float3 finalColor = lerp(refractionColor, finalColorPreFog, diffuseOutput.refractionMul);
#						if defined(WETNESS_EFFECTS) && defined(DEBUG_WETNESS_EFFECTS)
	// DEBUG MODE: Override water color with debug visualization
	float3 debugColor = WetnessEffects::GetDebugWetnessColorStandard(waterData.rippleInfo, 2.0, 3.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#						endif
#					endif

#				endif
#			endif
#		if defined(UNIFIED_WATER)
	// Apply physically-based foam with light blue color and lighting
	finalColor = lerp(finalColor, foamData.color, foamData.density);

	if (TriVisualizerEnabled > 0.5f) {
		const float cellWorldSize = max(CellWorldSize, 1.0f);
		float tileSpanCells = max(TileData.w, 1.0f);
		float2 tileOrigin = float2(TileData.x, TileData.y) * cellWorldSize;
		float tileSpanWorld = tileSpanCells * cellWorldSize;
		float2 localPos = (input.WPosition.xz - tileOrigin) / max(tileSpanWorld, 1.0f);

		float segmentsPerAxis = max(PrevData.w, 1.0f);
		float2 fineCoord = localPos * segmentsPerAxis;
		float2 fineFrac = frac(fineCoord);
		float2 fineDist = min(fineFrac, 1.0f - fineFrac);
		float2 fineFw = max(fwidth(fineCoord), 1e-4.xx);
		float2 fineLine = saturate(1.0f - smoothstep(fineFw * 0.35f, fineFw * 0.9f, fineDist));
		float fineHighlight = saturate(max(fineLine.x, fineLine.y));

		float2 cellCoord = localPos * tileSpanCells;
		float2 cellFrac = frac(cellCoord);
		float2 cellDist = min(cellFrac, 1.0f - cellFrac);
		float2 cellFw = max(fwidth(cellCoord), 1e-4.xx);
		float2 majorLine = saturate(1.0f - smoothstep(cellFw * 0.45f, cellFw * 1.1f, cellDist));
		float majorHighlight = saturate(max(majorLine.x, majorLine.y));

		float3 fineColor = float3(0.95f, 0.45f, 0.15f);
		float3 majorColor = float3(0.15f, 0.65f, 0.95f);

		finalColor = lerp(finalColor, majorColor, majorHighlight * 0.5f);
		finalColor = lerp(finalColor, fineColor, fineHighlight);
	}
	
#		endif
	psout.Lighting = float4(finalColor, isSpecular);
	
	// DEBUG: Show parallax offset for ALL water types (uncomment to test)
#if defined(WATER_PARALLAX)
	{
	    float2 debugParallaxOffset = float2(0, 0);
	    float visualizationScale = 10.0; // Different scale for different water types
	    
#if defined(FLOWMAP)
	    // For flowmap water, get the flowmap parallax offset
	    FlowmapData flowData = GetFlowmapDataUV(input, float2(0, 0));
	    float offset = 0.0;
	    float multiplier = 9.92;
	    float2 baseUV = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));
	    debugParallaxOffset = WaterEffects::GetFlowmapParallaxOffset(input, baseUV);
	    visualizationScale = 100.0; // Flowmap needs higher multiplier
#else
	    // For regular water, get the standard parallax offset
	    float3 normalScalesRcp = rcp(input.NormalsScale.xyz);
	    debugParallaxOffset = WaterEffects::GetParallaxOffset(input, normalScalesRcp);
	    visualizationScale = 1.0; // Regular water needs lower multiplier
#endif
	    
	    // Show offset components scaled up for visibility
	    // Red = X offset, Green = Y offset, Blue = magnitude
	    float magnitude = length(debugParallaxOffset);
	    float4 debugColor = float4(
	        abs(debugParallaxOffset.x) * visualizationScale,
	        abs(debugParallaxOffset.y) * visualizationScale,
	        magnitude * visualizationScale,
	        1.0
	    );
	    
	    // Output to correct render target based on shader variant
#if defined(STENCIL)
	    psout.WaterMask = debugColor;
#else
	    psout.Lighting = debugColor;
#endif
	}
#endif
// END DEBUG
#		endif

#		if defined(STENCIL)
	float3 viewDirection = normalize(input.WorldPosition.xyz);
	float3 normal =
		normalize(cross(ddx_coarse(input.WorldPosition.xyz), ddy_coarse(input.WorldPosition.xyz)));
	float VdotN = dot(viewDirection, normal);
	psout.WaterMask = float4(0, 0, VdotN, 0);

	psout.MotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
#		endif

	return psout;
}

#	endif

#endif