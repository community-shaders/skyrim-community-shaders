namespace GrassCollision
{
	Texture2D<float4> Collision : register(t100);

#ifndef GRASS_COLLISION_CBUFFER_REGISTER
#	define GRASS_COLLISION_CBUFFER_REGISTER b5
#endif

	cbuffer GrassCollisionPerFrame : register(GRASS_COLLISION_CBUFFER_REGISTER)
	{
		float2 PosOffset;   // cell origin in camera space
		uint2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

		int2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;

		float CameraHeightDelta;
	}

	const static uint TEXTURE_SIZE = 512;
	const static float WORLD_SIZE = 4096;
	const static float CELL_SIZE = WORLD_SIZE / TEXTURE_SIZE;
	const static float2 ZRANGE = float2(2048.0, -2048.0);

	float ProceduralAnimation(float x, float distanceFromCenter, bool smoothRecovery, float recoveryRate)
	{
		float fadeRate = 250;
		x /= fadeRate;
		x /= distanceFromCenter;
		x *= 100;

		if (smoothRecovery) {
			float recovery = exp(-max(x, 0.0) * recoveryRate);
			return recovery * recovery * (3.0 - 2.0 * recovery);
		}

		float frequency = 4 * Math::PI;
		return cos(x * frequency) * exp(-x * 4);
	}

	void GetCollision(float3 worldPosition, float maximumDepth, float distanceFromCenter, bool smoothRecovery, float recoveryRate, out float collisionHeights, out float collisionAmount, out float previousCollisionHeights, out float previousCollisionAmount)
	{
		float2 positionMSAdjusted = worldPosition.xy - PosOffset.xy;
		float2 uv = positionMSAdjusted / WORLD_SIZE + .5;

		float2 cellVxCoord = uv * TEXTURE_SIZE;
		int2 cell000 = floor(cellVxCoord - 0.5);
		float2 bilinearPos = cellVxCoord - 0.5 - cell000;

		int2 cellID = cell000;

		collisionHeights = 0.0;
		collisionAmount = 0.0;

		previousCollisionHeights = 0.0;
		previousCollisionAmount = 0.0;

		float wsum = 0;

		for (int i = 0; i < 2; i++)
			for (int j = 0; j < 2; j++) {
				int2 offset = int2(i, j);
				int2 cellID = cell000 + offset;

				if (any(cellID < 0) || any((uint2)cellID >= TEXTURE_SIZE))
					continue;

				float2 cellCentreMS = cellID + 0.5 - TEXTURE_SIZE / 2;
				cellCentreMS = cellCentreMS * CELL_SIZE;

				float2 bilinearWeights = 1 - abs(offset - bilinearPos);
				float w = bilinearWeights.x * bilinearWeights.y;

				uint2 cellTexID = (cellID + ArrayOrigin.xy) % TEXTURE_SIZE;

				float4 collisionSample = Collision[cellTexID];
				collisionSample = lerp(ZRANGE.x, ZRANGE.y, collisionSample);

				collisionHeights += collisionSample.x * w;
				const float penetration = max(0, min(maximumDepth, worldPosition.z - collisionSample.x));
				[branch] if (penetration > 0.0)
					collisionAmount += penetration * ProceduralAnimation(collisionSample.y - collisionSample.x, distanceFromCenter, smoothRecovery, recoveryRate) * w;

#if !defined(GRASS_COLLISION_CURRENT_ONLY)
				previousCollisionHeights += collisionSample.z * w;
				const float previousPenetration = max(0, min(maximumDepth, worldPosition.z - collisionSample.z));
				[branch] if (previousPenetration > 0.0)
					previousCollisionAmount += previousPenetration * ProceduralAnimation(collisionSample.w - collisionSample.z, distanceFromCenter, smoothRecovery, recoveryRate) * w;
#endif

				wsum += w;
			}

		if (wsum > 0.0) {
			collisionHeights /= wsum;
			collisionAmount /= wsum;
#if !defined(GRASS_COLLISION_CURRENT_ONLY)
			previousCollisionHeights /= wsum;
			previousCollisionAmount /= wsum;
#endif
		} else {
			collisionHeights = TEXTURE_SIZE;
			collisionAmount = 0.0;
#if !defined(GRASS_COLLISION_CURRENT_ONLY)
			previousCollisionHeights = TEXTURE_SIZE;
			previousCollisionAmount = 0.0;
#endif
		}
	}

	float3 ComputeNormalFromHeights(float h0, float hX, float hY, float delta)
	{
		float3 tangentX = float3(2.0 * delta, delta, hX - h0);
		float3 tangentY = float3(delta, 2.0 * delta, hY - h0);
		float3 crossProd = cross(tangentX, tangentY) * float3(1.0, 1.0, 0.1 / 3.0);

		float lenSq = dot(crossProd, crossProd);
		return lenSq > 1e-12 ? -crossProd * rsqrt(lenSq) : float3(0, 0, -1);
	}

	void ComputeCollision(float3 worldPosition, float maximumDepth, float distanceFromCenter, float delta, bool smoothRecovery, float recoveryRate, out float3 collision, out float3 previousCollision)
	{
		// Three samples estimate the collision surface normal.
		float collisionCenter;
		float collisionX;
		float collisionY;

		float collisionCenterAmount;
		float collisionXAmount;
		float collisionYAmount;

		float previousCollisionCenter;
		float previousCollisionX;
		float previousCollisionY;

		float previousCollisionCenterAmount;
		float previousCollisionXAmount;
		float previousCollisionYAmount;

		GetCollision(worldPosition + float3(-delta, -delta, 0), maximumDepth, distanceFromCenter, smoothRecovery, recoveryRate, collisionCenter, collisionCenterAmount, previousCollisionCenter, previousCollisionCenterAmount);
		GetCollision(worldPosition + float3(delta, 0, 0), maximumDepth, distanceFromCenter, smoothRecovery, recoveryRate, collisionX, collisionXAmount, previousCollisionX, previousCollisionXAmount);
		GetCollision(worldPosition + float3(0, delta, 0), maximumDepth, distanceFromCenter, smoothRecovery, recoveryRate, collisionY, collisionYAmount, previousCollisionY, previousCollisionYAmount);

		float3 currentAmounts = float3(collisionCenterAmount, collisionXAmount, collisionYAmount);
		float avgCurrentAmount = dot(currentAmounts, float3(1.0, 1.0, 1.0)) / 3.0;
		collision = ComputeNormalFromHeights(collisionCenter, collisionX, collisionY, delta) * avgCurrentAmount;

#if !defined(GRASS_COLLISION_CURRENT_ONLY)
		// Process previous collision
		float3 previousAmounts = float3(previousCollisionCenterAmount, previousCollisionXAmount, previousCollisionYAmount);
		float avgPreviousAmount = dot(previousAmounts, float3(1.0, 1.0, 1.0)) / 3.0;
		previousCollision = ComputeNormalFromHeights(previousCollisionCenter, previousCollisionX, previousCollisionY, delta) * avgPreviousAmount;
#else
		previousCollision = 0.0;
#endif
	}

	/** @param nearFactor Fade of the collision response with camera distance. */
	void GetDisplacedPosition(float3 worldPosition, float3 worldPositionCentre, float alpha, float nearFactor, bool smoothRecovery, float recoveryRate, out float3 displacement, out float3 previousDisplacement)
	{
		if (alpha > 0.0 && nearFactor > 0.0) {
			// Limit stretching
			float3 remappedWorldPosition = lerp(worldPosition, worldPositionCentre, float3(0.95, 0.95, 0.0));

			float distanceFromCenter = length(worldPosition - worldPositionCentre) + 0.01;
			float maximumDepth = worldPosition.z - worldPositionCentre.z;

			float3 collision, previousCollision;

			float collisionDelta = smoothRecovery ? CELL_SIZE * 1.5 : CELL_SIZE;
			ComputeCollision(remappedWorldPosition, maximumDepth, distanceFromCenter, collisionDelta, smoothRecovery, recoveryRate, collision, previousCollision);

			collision.z = -abs(collision.z);
#if !defined(GRASS_COLLISION_CURRENT_ONLY)
			previousCollision.z = -abs(previousCollision.z);
#endif

			displacement = collision * alpha * nearFactor * 0.75;
#if defined(GRASS_COLLISION_CURRENT_ONLY)
			previousDisplacement = 0.0;
#else
			previousDisplacement = previousCollision * alpha * nearFactor * 0.75;
#endif
		} else {
			displacement = 0.0;
			previousDisplacement = 0.0;
		}
	}

#ifdef GRASS_COLLISION_REGULAR_GRASS
#	ifdef GRASS_OPTIMIZATIONS
	// Positions arrive camera-relative: instances are already in world space here, so applying World again would double the offset.
	void GetDisplacedPosition(VS_INPUT input, float3 worldPosition, float3 worldPositionCentre, out float3 displacement, out float3 previousDisplacement)
	{
		GetDisplacedPosition(worldPosition, worldPositionCentre, saturate(input.Color.w * 10.0), smoothstep(2048.0, 0.0, length(worldPosition)), false, 1.5, displacement, previousDisplacement);
	}
#	else
	void GetDisplacedPosition(VS_INPUT input, float3 position, out float3 displacement, out float3 previousDisplacement)
	{
		float3 worldPosition = mul(World, float4(position.xyz, 1.0)).xyz;
		float3 worldPositionCentre = mul(World, float4(input.InstanceData1.xyz, 1.0)).xyz;
		GetDisplacedPosition(worldPosition, worldPositionCentre, saturate(input.Color.w * 10.0), smoothstep(2048.0, 0.0, length(worldPosition)), false, 1.5, displacement, previousDisplacement);
	}
#	endif
#endif
}
