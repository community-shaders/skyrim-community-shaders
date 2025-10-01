namespace GrassCollision
{
	Texture2D<float2> Collision : register(t100);
	Texture2D<float2> CollisionNormal : register(t101);

	SamplerState CollisionSampler : register(s0);  // Bilinear sampler

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

	const static uint2 ARRAY_DIM = uint2(1024, 1024);
	const static float2 ARRAY_SIZE = float2(4096.0, 4096.0);

	const static float2 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;

	float ElasticFunction(float x) {
		float frequency = 3 * Math::PI;
		float dampening = 1;
		x = 1.0 - x;
		return sin(x * frequency) * (1.0 - x) * exp(-x * dampening);
	}
	
	float ComputeCollisionAmountElastic(float3 worldPosition, float2 collision)
	{
		float collisionDepth = saturate(max(0, worldPosition.z - collision.x) / 50.0);
		return collisionDepth * ElasticFunction(saturate((collision.x + 2.0 - collision.y) / 2.0));
	}
	
	float ComputeCollisionAmount(float3 worldPosition, float2 collision)
	{
		float collisionDepth = saturate(max(0, worldPosition.z - collision.x) / 50.0);
		return collisionDepth * saturate((collision.x + 2.0 - collision.y) / 2.0);
	}

	void GetCollision(float3 worldPosition, out float2 collision, out float3 collisionNormal)
	{
		float2 positionMSAdjusted = worldPosition - PosOffset.xy;
		float2 uv = positionMSAdjusted / ARRAY_SIZE + .5;

		float2 cellVxCoord = uv * ARRAY_DIM;
		int2 cell000 = floor(cellVxCoord - 0.5);
		float2 bilinearPos = cellVxCoord - 0.5 - cell000;

		int2 cellID = cell000;
		
		collision = 0.0;
		collisionNormal = float3(0.0, 0.0, 0.0);

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

			float collisionAmount = ComputeCollisionAmount(worldPosition, collisionSample);

			if (collisionAmount > 0.0){
				float3 collisionNormalSample;
				collisionNormalSample.xy = CollisionNormal[cellTexID] * 2.0 - 1.0;
				// Recompute Z
				collisionNormalSample.z = -sqrt(saturate(1.0 - dot(collisionNormalSample.xy, collisionNormalSample.xy)));
				// Weighted by if the normal is a valid collision
				collisionNormal += lerp(float3(0, 0, -1), collisionNormalSample, collisionAmount) * w;
			} else {
				collisionNormal += float3(0, 0, -1) * w;
			}

			collision += collisionSample * w;

			wsum += w;
		}
		
		if (wsum > 0.0)
			collision /= wsum;
		else 
			collision = 1000000;
				
		if (length(collisionNormal) > 0.0)
			collisionNormal = normalize(collisionNormal);
		else
			collisionNormal = float3(0, 0, -1);
	}

	float3 GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position, 1.0)).xyz;

		if (input.Color.w > 0.0) {
			float2 collision;
			float3 collisionNormal;
			GetCollision(worldPosition, collision, collisionNormal);

			float3 displacement = collisionNormal * ComputeCollisionAmountElastic(worldPosition, collision);

			// Bounce around the Z axis
			displacement.z = -abs(displacement.z);

			// Scale grass by mesh
			float bendability = max(1.0, input.Position.z + 1.0);
			displacement.xyz *= bendability;
			
			// Scale grass by wind amount (detect rocks and bottom of some grass)
			float alpha = saturate(input.Color.w * 10.0); 

			return displacement * alpha;
		}

		return 0.0;
	}
}