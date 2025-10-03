namespace GrassCollision
{
	Texture2D<float2> Collision : register(t100);

	struct CollisionData
	{
		float4 centre[2];
	};

	cbuffer GrassCollisionPerFrame : register(b5)
	{
		float2 PosOffset;  // cell origin in camera space
		uint2 ArrayOrigin; // xy: array origin (clipmap wrapping)

		float timeDelta;
		float3 eyePosition;

		CollisionData collisionData[256];
		uint numCollisions;
		uint pad0[3];
	}

	const static uint2 ARRAY_DIM = uint2(512, 512);
	const static float2 ARRAY_SIZE = float2(4096.0, 4096.0);

	const static float2 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;
	const static float2 ZRANGE = float2(1024.0, -1024.0);
	
	float ProceduralAnimation(float x) {
		float fadeRate = 10;
		x /= fadeRate;
		float frequency = 4 * Math::PI;
		float dampening = 3;
		return cos(x * frequency) * exp(-x * dampening);
	}

	void GetCollision(float3 worldPosition, out float2 collisionHeights, out float collisionAmount)
	{
		float2 positionMSAdjusted = worldPosition - PosOffset.xy;
		float2 uv = positionMSAdjusted / ARRAY_SIZE + .5;

		float2 cellVxCoord = uv * ARRAY_DIM;
		int2 cell000 = floor(cellVxCoord - 0.5);
		float2 bilinearPos = cellVxCoord - 0.5 - cell000;

		int2 cellID = cell000;
		
		collisionHeights = 0.0;
		collisionAmount = 0.0;

		float wsum = 0;
		
		for (int i = 0; i < 2; i++)
			for (int j = 0; j < 2; j++)
		{
			int2 offset = int2(i, j);
			int2 cellID = cell000 + offset;

			if (any(cellID < 0) || any((uint2)cellID >= ARRAY_DIM))
				continue;

			float2 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
			cellCentreMS = cellCentreMS * CELL_SIZE;

			float2 bilinearWeights = 1 - abs(offset - bilinearPos);
			float w = bilinearWeights.x * bilinearWeights.y;

			uint2 cellTexID = (cellID + ArrayOrigin.xy) % ARRAY_DIM;
			
			float2 collisionSample = Collision[cellTexID];
			collisionSample = lerp(ZRANGE.x, ZRANGE.y, collisionSample);
			
			collisionSample.x = min(collisionSample.x, collisionSample.y);

			float collisionDepth = max(0, worldPosition.z - collisionSample.x);

			collisionHeights += collisionSample.x * w;
			collisionAmount += collisionDepth * ProceduralAnimation(collisionSample.y - collisionSample.x) * w;

			wsum += w;
		}
		
		if (wsum > 0.0){
			collisionHeights /= wsum;
			collisionAmount /= wsum;
		} else {
			collisionHeights = ARRAY_DIM.x;
			collisionAmount = 0.0;
		}
	}

	float3 ComputeCollision(float3 worldPosition, float delta)
	{
		// Sample collision at three points forming a small triangle
		float2 collisionCenter;
		float2 collisionX;
		float2 collisionY;
		
		float collisionCenterAmount;
		float collisionXAmount;
		float collisionYAmount;

		GetCollision(worldPosition + float3(-delta, -delta, 0), collisionCenter, collisionCenterAmount);
		GetCollision(worldPosition + float3(delta, 0, 0), collisionX, collisionXAmount);
		GetCollision(worldPosition + float3(0, delta, 0), collisionY, collisionYAmount);
		
		// Use the collision data as height (using .x component - adjust if needed)
		float h0 = collisionCenter.x;
		float hX = collisionX.x;
		float hY = collisionY.x;
		
		// Compute tangent vectors in 3D space
		float3 tangentX = float3(delta, 0, hX - h0);
		float3 tangentY = float3(0, delta, hY - h0);

		// Cross product gives the full normal
		float3 collisionNormal = cross(tangentX, tangentY);
		collisionNormal.z *= 0.1;

		// Average collision samples
		float collisionAmount = (collisionCenterAmount + collisionXAmount + collisionYAmount) / 3.0;
		
		// Normalize the result
		return -normalize(collisionNormal) * collisionAmount;
	}

	float3 GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position.xyz, 1.0)).xyz;

		if (input.Color.w > 0.0 && length(worldPosition) < 2048.0) {
			float3 worldPositionCentre = mul(World[eyeIndex], float4(input.InstanceData1.xyz, 1.0)).xyz;

			// Limit stretching
			worldPosition.xy = lerp(worldPosition.xy, worldPositionCentre.xy, 0.95);
	
			// Return base collision
			float3 collision = ComputeCollision(worldPosition, CELL_SIZE * 0.5);

			// Bounce around the Z axis
			collision.z = -abs(collision.z);

			// Grass bends depending on how much it is supporting
			collision.z -= length(collision.xy) * length(worldPosition.xy - worldPositionCentre.xy) * 0.5;

			// Scale grass by wind amount (detect rocks and bottom of some grass)
			float alpha = saturate(input.Color.w * 10.0); 

			return collision * alpha * 0.75;			
		}

		return 0.0;
	}
}