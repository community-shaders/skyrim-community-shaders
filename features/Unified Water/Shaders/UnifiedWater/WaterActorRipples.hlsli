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
	// Constants matching vanilla wading behavior
	static const float MAX_RIPPLE_RADIUS = 512.0f;   // Maximum expansion radius
	
	// Kelvin wake angle: ~19.47 degrees (arcsin(1/3))
	// This is the angle at which wake waves propagate from a moving object
	static const float KELVIN_ANGLE = 0.3398f;       // radians (~19.47°)
	
	// Hash function for procedural variation
	float hash(float2 p)
	{
		float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}
	
	// Calculate wake-shaped ripples with V-pattern behind moving actor
	// This creates the characteristic arrowhead/chevron wake pattern
	float4 EvaluateWakeRipples(float2 worldPos, float2 actorPos, float2 moveDir, float time, float speed, float baseStrength)
	{
		float2 toPos = worldPos - actorPos;
		float dist = length(toPos);
		
		if (dist > MAX_RIPPLE_RADIUS || dist < 0.001f)
			return float4(0, 0, 1, 0);
		
		float2 dir = toPos / dist;
		
		// Position relative to movement direction
		float forward = dot(dir, moveDir);      // +1 = directly in front, -1 = directly behind
		float2 perpDir = float2(-moveDir.y, moveDir.x);
		float sideways = dot(dir, perpDir);     // Signed lateral distance
		float absSideways = abs(sideways);
		
		// Speed factor affects wake intensity and shape
		float speedFactor = saturate(speed / 300.0f);
		
		// === BOW WAVE (in front) ===
		float bowWave = 0.0f;
		float bowGradient = 0.0f;
		if (forward > 0.1f) {
			// Bow wave: compressed waves directly in front
			float bowDist = dist * (1.0f + absSideways * 2.0f);  // Narrower at sides
			float bowPhase = bowDist * 0.15f - time * 6.0f;
			bowWave = sin(bowPhase) * exp(-bowDist * 0.015f);
			bowWave *= forward * speedFactor;  // Stronger when more directly in front
			bowWave *= smoothstep(MAX_RIPPLE_RADIUS * 0.3f, 0.0f, dist);  // Only close to actor
			
			bowGradient = cos(bowPhase) * 0.15f * exp(-bowDist * 0.015f) * forward * speedFactor;
		}
		
		// === KELVIN WAKE (V-shape behind) ===
		float wakeWave = 0.0f;
		float2 wakeGradientDir = dir;
		float wakeGradient = 0.0f;
		if (forward < 0.3f) {
			float behind = max(0.0f, -forward + 0.3f);  // How far behind (0 at sides, 1 at back)
			
			// V-wake arms: waves propagate at Kelvin angle from movement direction
			// The wake pattern forms along lines at ~19.47° from the path
			float wakeAngle = atan2(absSideways, behind + 0.001f);
			
			// Wake is strongest along the Kelvin angle lines
			float angleFromKelvin = abs(wakeAngle - KELVIN_ANGLE);
			float wakeArmStrength = exp(-angleFromKelvin * angleFromKelvin * 8.0f);
			
			// Transverse waves (perpendicular to wake arms)
			float armDist = dist * (0.5f + behind * 0.5f);
			float transversePhase = armDist * 0.08f - time * 4.0f;
			float transverseWave = sin(transversePhase);
			
			// Divergent waves (along wake arms, create the V pattern)
			float divergentPhase = (absSideways * 3.0f + behind * dist * 0.02f) - time * 3.0f;
			float divergentWave = sin(divergentPhase) * 0.7f;
			
			// Combine wake components
			wakeWave = (transverseWave * 0.6f + divergentWave * 0.4f) * wakeArmStrength;
			wakeWave *= behind;  // Fade toward sides
			wakeWave *= speedFactor;
			
			// Distance falloff for wake
			float wakeFalloff = exp(-dist * 0.003f) * (1.0f - smoothstep(MAX_RIPPLE_RADIUS * 0.7f, MAX_RIPPLE_RADIUS, dist));
			wakeWave *= wakeFalloff;
			
			// Wake gradient for normal calculation
			// Gradient points along the wake arm direction
			float armAngle = atan2(sideways, -forward + 0.001f);
			wakeGradientDir = float2(cos(armAngle), sin(armAngle));
			wakeGradient = cos(transversePhase) * 0.08f * wakeArmStrength * behind * speedFactor * wakeFalloff;
		}
		
		// === TURBULENT CENTER (directly behind) ===
		float turbulence = 0.0f;
		float turbGradient = 0.0f;
		if (forward < -0.5f && absSideways < 0.4f) {
			// Churning water directly behind the actor
			float turbDist = dist;
			float turbPhase1 = turbDist * 0.2f - time * 8.0f + hash(actorPos) * 6.28f;
			float turbPhase2 = turbDist * 0.15f - time * 6.0f + hash(actorPos.yx) * 6.28f;
			
			turbulence = (sin(turbPhase1) * 0.5f + sin(turbPhase2) * 0.3f);
			turbulence *= (1.0f - absSideways / 0.4f);  // Fade toward edges
			turbulence *= smoothstep(MAX_RIPPLE_RADIUS * 0.4f, 0.0f, dist);  // Close range only
			turbulence *= speedFactor * 1.2f;
			
			turbGradient = (cos(turbPhase1) * 0.5f * 0.2f + cos(turbPhase2) * 0.3f * 0.15f);
			turbGradient *= (1.0f - absSideways / 0.4f) * speedFactor;
		}
		
		// === COMBINE ALL WAVE COMPONENTS ===
		float totalWave = bowWave + wakeWave + turbulence;
		totalWave *= baseStrength;
		
		// Distance-based overall falloff
		float overallFalloff = 1.0f - smoothstep(0.0f, MAX_RIPPLE_RADIUS, dist);
		overallFalloff *= overallFalloff;
		totalWave *= overallFalloff;
		
		// Near boost for immediate splash effect
		float nearBoost = 1.0f + smoothstep(64.0f, 0.0f, dist) * 2.0f;
		totalWave *= nearBoost;
		
		// Calculate combined gradient for normal
		float2 gradientDir = dir;
		float totalGradient = 0.0f;
		
		// Blend gradients based on wave contributions
		float bowWeight = abs(bowWave);
		float wakeWeight = abs(wakeWave);
		float turbWeight = abs(turbulence);
		float totalWeight = bowWeight + wakeWeight + turbWeight + 0.001f;
		
		totalGradient = (bowGradient * bowWeight + wakeGradient * wakeWeight + turbGradient * turbWeight) / totalWeight;
		totalGradient *= baseStrength * overallFalloff * nearBoost;
		
		// Blend gradient directions
		if (wakeWeight > bowWeight && wakeWeight > turbWeight) {
			gradientDir = lerp(dir, wakeGradientDir, 0.5f);
		}
		
		float3 normal = normalize(float3(-gradientDir * totalGradient, 1.0f));
		
		return float4(normal, totalWave);
	}
	
	// Gentle circular ripples for stationary actors
	float4 GetStationaryRipples(float2 actorPos, float2 worldPos, float time, float rippleStrength)
	{
		float2 toPos = worldPos - actorPos;
		float dist = length(toPos);
		
		if (dist > MAX_RIPPLE_RADIUS)
			return float4(0, 0, 1, 0);
		
		// Gentle circular waves for standing still
		// Higher frequencies for more visible ripples
		float wave1 = sin(dist * 0.12f - time * 3.0f);
		float wave2 = sin(dist * 0.18f - time * 4.0f) * 0.6f;
		
		float combinedWave = (wave1 + wave2) / 1.6f;
		
		// Distance falloff - extend the effective range
		float falloff = 1.0f - smoothstep(0.0f, MAX_RIPPLE_RADIUS * 0.7f, dist);
		falloff *= falloff;
		
		float amplitude = combinedWave * falloff * rippleStrength;
		
		float2 dir = dist > 0.001f ? toPos / dist : float2(0, 1);
		float waveGradient = cos(dist * 0.12f - time * 3.0f) * 0.12f +
		                     cos(dist * 0.18f - time * 4.0f) * 0.6f * 0.18f;
		waveGradient /= 1.6f;
		waveGradient *= falloff * rippleStrength;
		
		float3 normal = normalize(float3(-dir * waveGradient * 2.0f, 1.0f));
		
		return float4(normal, amplitude);
	}

	// Reorient a ripple normal onto the base water normal
	float3 ApplyRippleNormal(float3 rippleNormal, float3 baseNormal)
	{
		float3 result = baseNormal;
		result.xy += rippleNormal.xy * 1.5f;
		return normalize(result);
	}
	
	// Calculate ripples from a single actor position
	// Creates realistic V-shaped wake patterns based on movement
	float4 GetActorRipples(float2 actorPos, float2 worldPos, float time, float actorSpeed, float inWater)
	{
		if (inWater < 0.5f)
			return float4(0, 0, 1, 0);
			
		float2 toPos = worldPos - actorPos;
		float dist = length(toPos);
		
		if (dist > MAX_RIPPLE_RADIUS)
			return float4(0, 0, 1, 0);
		
		float baseStrength = lerp(0.1f, 0.8f, saturate(actorSpeed / 300.0f));
		float speedFactor = saturate(actorSpeed / 300.0f);
		
		// For stationary or very slow actors, use gentle circular ripples
		if (speedFactor < 0.1f) {
			return GetStationaryRipples(actorPos, worldPos, time, baseStrength);
		}
		
		// For moving actors, create wake pattern with pseudo-direction
		// Since we don't have actual movement direction, derive it from position hash + time
		float dirAngle = hash(actorPos) * 6.28318f + time * 0.3f;
		float2 pseudoMoveDir = float2(cos(dirAngle), sin(dirAngle));
		
		return EvaluateWakeRipples(worldPos, actorPos, pseudoMoveDir, time, actorSpeed, baseStrength);
	}
	
	// Main function: Calculate ripples from ALL actors (player + NPCs)
	float4 GetAllActorRipples(float2 worldPos, float time, float2 playerPos, float playerSpeed, float playerInWater)
	{
		float3 resultNormal = float3(0, 0, 1);
		float resultHeight = 0.0f;
		float totalWeight = 0.0f;
		
		// Player ripples - use wake pattern
		if (playerInWater > 0.5f) {
			// Base strength: minimum 0.5 for visibility, up to 1.0 with speed
			float baseStrength = lerp(0.5f, 1.0f, saturate(playerSpeed / 300.0f));
			float speedFactor = saturate(playerSpeed / 300.0f);
			
			float4 playerResult;
			if (speedFactor < 0.1f) {
				// Stationary player gets gentle circular ripples
				playerResult = GetStationaryRipples(playerPos, worldPos, time, baseStrength);
			} else {
				// Moving player gets wake pattern
				// Use a consistent direction based on position for visual stability
				float dirAngle = hash(playerPos * 0.01f) * 6.28318f + time * 0.2f;
				float2 pseudoMoveDir = float2(cos(dirAngle), sin(dirAngle));
				playerResult = EvaluateWakeRipples(worldPos, playerPos, pseudoMoveDir, time, playerSpeed, baseStrength);
			}
			
			if (playerResult.w != 0.0f) {
				float weight = abs(playerResult.w);
				resultNormal.xy += playerResult.xy * weight;
				resultHeight += playerResult.w;
				totalWeight += weight;
			}
		}
		
		// Add ripples from all tracked NPCs
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
