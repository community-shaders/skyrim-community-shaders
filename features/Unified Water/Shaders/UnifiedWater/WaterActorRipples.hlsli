#ifndef __WATER_ACTOR_RIPPLES_HLSLI__
#define __WATER_ACTOR_RIPPLES_HLSLI__

// Player Ripples System for Unified Water
// Generates procedural wading ripples around actors (player and NPCs) in water
// Based on the vanilla ISWaterDisplacement wading system and WetnessEffects ripple approach

#define MAX_ACTOR_RIPPLES 32

struct ActorRippleData
{
	float PosX;
	float PosY;
	float Speed;
	float InWater;
};

cbuffer ActorRippleBuffer : register(b10)
{
	ActorRippleData ActorRipples[MAX_ACTOR_RIPPLES];
	uint NumActorRipples;
	uint ActorRipplePad0;
	uint ActorRipplePad1;
	uint ActorRipplePad2;
}

namespace PlayerRipples
{
	// Ripple ring structure for expanding circular waves
	struct RippleRing
	{
		float2 center;      // World position of ripple origin
		float startTime;    // When the ripple started
		float strength;     // Initial strength (affected by movement speed)
	};

	// Constants matching vanilla wading behavior
	static const float RIPPLE_SPEED = 150.0f;        // Units per second ripple expansion
	static const float RIPPLE_LIFETIME = 3.0f;       // Seconds until ripple fades
	static const float RIPPLE_WAVELENGTH = 64.0f;    // Distance between wave peaks
	static const float RIPPLE_DECAY = 2.0f;          // How fast amplitude decreases with distance
	static const float MAX_RIPPLE_RADIUS = 512.0f;   // Maximum expansion radius
	
	// Hash function for procedural variation
	float hash(float2 p)
	{
		float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	// Smooth derivative for ripple shape (from WetnessEffects)
	float SmoothstepDeriv(float x)
	{
		return 6.0f * x * (1.0f - x);
	}

	// Fade function for ripple decay over lifetime
	float RippleFade(float normalizedTime)
	{
		// Quadratic fade out
		float inv = 1.0f - saturate(normalizedTime);
		return inv * inv;
	}

	// Calculate a single expanding ring ripple
	// Returns: xyz = normal perturbation, w = height displacement
	float4 EvaluateRippleRing(float2 worldPos, float2 rippleCenter, float rippleAge, float rippleStrength)
	{
		float2 toPos = worldPos - rippleCenter;
		float dist = length(toPos);
		
		// Current ripple radius based on expansion speed
		float currentRadius = rippleAge * RIPPLE_SPEED;
		
		// Skip if outside the wave front
		if (dist > currentRadius + RIPPLE_WAVELENGTH || dist < currentRadius - RIPPLE_WAVELENGTH * 2.0f)
			return float4(0, 0, 1, 0);
		
		// Skip if past maximum radius
		if (currentRadius > MAX_RIPPLE_RADIUS)
			return float4(0, 0, 1, 0);
		
		// Distance from the wave front
		float waveDist = dist - currentRadius;
		
		// Wave shape: damped sinusoid
		float wavePhase = waveDist / RIPPLE_WAVELENGTH * 6.28318530f;
		float waveValue = sin(wavePhase);
		
		// Amplitude envelope: strongest at wave front, fades behind
		float envelope = exp(-abs(waveDist) / (RIPPLE_WAVELENGTH * 1.5f));
		
		// Distance attenuation: ripples get weaker as they expand
		float distAtten = 1.0f / (1.0f + dist * 0.005f);
		
		// Time fade
		float timeFade = RippleFade(rippleAge / RIPPLE_LIFETIME);
		
		// Combined amplitude
		float amplitude = waveValue * envelope * distAtten * timeFade * rippleStrength;
		
		// Calculate normal from wave gradient
		float2 dir = dist > 0.001f ? toPos / dist : float2(0, 1);
		
		// Gradient of wave (derivative of sin is cos)
		float waveGradient = cos(wavePhase) * envelope * distAtten * timeFade * rippleStrength;
		waveGradient *= 6.28318530f / RIPPLE_WAVELENGTH;  // Chain rule
		
		// Normal perturbation
		float3 normal = normalize(float3(-dir * waveGradient * 0.5f, 1.0f));
		
		return float4(normal, amplitude * 2.0f);  // Height in w
	}

	// Main function: Calculate player wading ripples
	// playerPos: Current player world position (xy)
	// playerPrevPos: Previous frame player position (xy) 
	// worldPos: Water surface position being shaded (xy)
	// time: Current time in seconds
	// Returns: xyz = ripple normal, w = height displacement
	float4 GetPlayerWadingRipples(
		float2 playerPos, 
		float2 playerPrevPos, 
		float2 worldPos, 
		float time,
		float playerSpeed,
		bool playerInWater)
	{
		float3 resultNormal = float3(0, 0, 1);
		float resultHeight = 0.0f;
		
		if (!playerInWater)
			return float4(resultNormal, resultHeight);
		
		// Calculate movement for ripple strength
		float2 movement = playerPos - playerPrevPos;
		float moveSpeed = length(movement);
		
		// Base ripple strength based on movement
		// Standing still = subtle ripples, moving = stronger
		float baseStrength = lerp(0.15f, 1.0f, saturate(playerSpeed / 300.0f));
		
		// Generate multiple ripple rings at intervals based on movement
		// More rings when moving faster
		int numRings = 4;
		float ringInterval = 0.25f;  // Seconds between ring spawns when moving
		
		[unroll]
		for (int i = 0; i < numRings; i++)
		{
			// Stagger ripple spawn times
			float ringAge = frac(time * (1.0f / ringInterval) + i * 0.25f) * RIPPLE_LIFETIME;
			
			// Interpolate spawn position based on movement
			float spawnT = float(i) / float(numRings);
			float2 ringCenter = lerp(playerPrevPos, playerPos, spawnT);
			
			// Add some variation to ring positions
			float2 offset = float2(
				hash(ringCenter + float2(i, time)) - 0.5f,
				hash(ringCenter + float2(time, i)) - 0.5f
			) * 32.0f;
			ringCenter += offset * (1.0f - baseStrength);  // Less variation when moving fast
			
			// Evaluate this ring
			float ringStrength = baseStrength * (1.0f - spawnT * 0.3f);  // Older rings slightly weaker
			float4 ringResult = EvaluateRippleRing(worldPos, ringCenter, ringAge, ringStrength);
			
			// Accumulate normal using reorientation
			if (ringResult.w != 0.0f)
			{
				// Simple normal blending
				resultNormal.xy += ringResult.xy * abs(ringResult.w);
				resultHeight += ringResult.w;
			}
		}
		
		// Normalize the accumulated normal
		resultNormal = normalize(resultNormal);
		
		return float4(resultNormal, resultHeight);
	}

	// Simplified version using just distance from player
	// For use when we don't have player movement tracking
	float4 GetSimplePlayerRipples(float2 playerPos, float2 worldPos, float time, float rippleStrength)
	{
		float2 toPos = worldPos - playerPos;
		float dist = length(toPos);
		
		// Only affect area near player
		if (dist > MAX_RIPPLE_RADIUS)
			return float4(0, 0, 1, 0);
		
		// Multiple wave frequencies for richer effect
		float wave1 = sin(dist * 0.08f - time * 4.0f);
		float wave2 = sin(dist * 0.12f - time * 5.5f) * 0.6f;
		float wave3 = sin(dist * 0.18f - time * 7.0f) * 0.3f;
		
		float combinedWave = (wave1 + wave2 + wave3) / 1.9f;
		
		// Distance falloff
		float falloff = 1.0f - smoothstep(0.0f, MAX_RIPPLE_RADIUS, dist);
		falloff *= falloff;  // Quadratic falloff
		
		// Close to player gets stronger ripples
		float nearBoost = 1.0f + smoothstep(128.0f, 0.0f, dist) * 3.0f;
		
		float amplitude = combinedWave * falloff * rippleStrength * nearBoost;
		
		// Calculate normal from wave gradient - increased strength
		float2 dir = dist > 0.001f ? toPos / dist : float2(0, 1);
		
		// Approximate gradient with stronger effect
		float waveGradient = cos(dist * 0.08f - time * 4.0f) * 0.08f +
		                     cos(dist * 0.12f - time * 5.5f) * 0.6f * 0.12f +
		                     cos(dist * 0.18f - time * 7.0f) * 0.3f * 0.18f;
		waveGradient /= 1.9f;
		waveGradient *= falloff * rippleStrength * nearBoost * 2.0f;  // Double the normal strength
		
		float3 normal = normalize(float3(-dir * waveGradient, 1.0f));
		
		return float4(normal, amplitude);
	}

	// Reorient a ripple normal onto the base water normal
	// Similar to WetnessEffects::ReorientNormal
	float3 ApplyRippleNormal(float3 rippleNormal, float3 baseNormal)
	{
		// Blend ripple XY components with base normal - use stronger blending
		float3 result = baseNormal;
		result.xy += rippleNormal.xy * 1.5f;  // Amplify the ripple effect
		return normalize(result);
	}
	
	// Calculate ripples from a single actor position (simplified version for NPCs)
	float4 GetActorRipples(float2 actorPos, float2 worldPos, float time, float actorSpeed, float inWater)
	{
		if (inWater < 0.5f)
			return float4(0, 0, 1, 0);
			
		float2 toPos = worldPos - actorPos;
		float dist = length(toPos);
		
		if (dist > MAX_RIPPLE_RADIUS)
			return float4(0, 0, 1, 0);
		
		// Strength based on movement speed
		float baseStrength = lerp(0.1f, 0.8f, saturate(actorSpeed / 300.0f));
		
		// Multiple wave frequencies
		float wave1 = sin(dist * 0.05f - time * 3.0f);
		float wave2 = sin(dist * 0.08f - time * 4.5f) * 0.5f;
		float wave3 = sin(dist * 0.12f - time * 6.0f) * 0.25f;
		
		float combinedWave = (wave1 + wave2 + wave3) / 1.75f;
		
		// Distance falloff
		float falloff = 1.0f - smoothstep(0.0f, MAX_RIPPLE_RADIUS, dist);
		falloff *= falloff;
		
		// Near boost
		float nearBoost = 1.0f + smoothstep(64.0f, 0.0f, dist) * 2.0f;
		
		float amplitude = combinedWave * falloff * baseStrength * nearBoost;
		
		// Calculate normal from wave gradient
		float2 dir = dist > 0.001f ? toPos / dist : float2(0, 1);
		
		float waveGradient = cos(dist * 0.05f - time * 3.0f) * 0.05f +
		                     cos(dist * 0.08f - time * 4.5f) * 0.5f * 0.08f +
		                     cos(dist * 0.12f - time * 6.0f) * 0.25f * 0.12f;
		waveGradient /= 1.75f;
		waveGradient *= falloff * baseStrength * nearBoost;
		
		float3 normal = normalize(float3(-dir * waveGradient, 1.0f));
		
		return float4(normal, amplitude);
	}
	
	// Main function: Calculate ripples from ALL actors (player + NPCs)
	// worldPos: Water surface position being shaded (xy)
	// time: Current time in seconds
	// playerPos/Speed/InWater: Player data from main cbuffer
	// Returns: xyz = ripple normal, w = height displacement
	float4 GetAllActorRipples(float2 worldPos, float time, float2 playerPos, float playerSpeed, float playerInWater)
	{
		float3 resultNormal = float3(0, 0, 1);
		float resultHeight = 0.0f;
		float totalWeight = 0.0f;
		
		// First add player ripples (always check player)
		if (playerInWater > 0.5f) {
			float4 playerResult = GetSimplePlayerRipples(playerPos, worldPos, time, 1.0f);
			if (playerResult.w != 0.0f) {
				float weight = abs(playerResult.w);
				resultNormal.xy += playerResult.xy * weight;
				resultHeight += playerResult.w;
				totalWeight += weight;
			}
		}
		
		// Add ripples from all tracked actors
		uint actorCount = min(NumActorRipples, MAX_ACTOR_RIPPLES);
		for (uint i = 0; i < actorCount; i++) {
			ActorRippleData actor = ActorRipples[i];
			float2 actorPos = float2(actor.PosX, actor.PosY);
			
			float4 actorResult = GetActorRipples(actorPos, worldPos, time, actor.Speed, actor.InWater);
			if (actorResult.w != 0.0f) {
				float weight = abs(actorResult.w);
				resultNormal.xy += actorResult.xy * weight;
				resultHeight += actorResult.w;
				totalWeight += weight;
			}
		}
		
		// Normalize accumulated normal
		if (totalWeight > 0.001f) {
			resultNormal = normalize(resultNormal);
		}
		
		return float4(resultNormal, resultHeight);
	}
}

#endif // __WATER_ACTOR_RIPPLES_HLSLI__
