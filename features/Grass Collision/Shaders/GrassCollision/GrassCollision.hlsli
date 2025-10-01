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
		float2 PosOffset;  // cell origin in camera model space
		uint2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

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
		float frequency = 50;
		float dampening = 5;
		x = saturate(1.0 - x);
		return sin(x * frequency) * (1.0 - x) * exp(-x * dampening);
	}

	float ComputeCollisionAmount(float3 worldPosition, float2 collision)
	{
		float collisionAmount = max(0, worldPosition.z - collision.x);
		return collisionAmount * saturate((collision.x + 10.0 - collision.y) / 10.0);
	}

	float ComputeCollisionAmountElastic(float3 worldPosition, float2 collision)
	{
		float collisionAmount = max(0, worldPosition.z - collision.x);
		return collisionAmount * ElasticFunction(saturate((collision.x + 10.0 - collision.y) / 10.0));
	}
	
	float ComputeCollisionAmountBent(float3 worldPosition, float2 collision)
	{
		float collisionAmount = max(0, worldPosition.z - collision.y);
		return collisionAmount * saturate((collision.x + 10.0 - collision.y) / 10.0);
	}

	void GetCollision(float3 worldPosition, out float2 collision, out float3 collisionNormal)
	{
		// Convert world space position to clipmap coordinates
		float2 positionMSAdjusted = worldPosition - PosOffset.xy;
		float2 uv = positionMSAdjusted / ARRAY_SIZE + .5;

		float2 cellVxCoord = uv * ARRAY_DIM;
		int2 cell000 = floor(cellVxCoord - 0.5);
		float2 bilinearPos = cellVxCoord - 0.5 - cell000;

		int2 cellID = cell000;
		
		collision = 0.0;
		collisionNormal = float3(0.0, 0.0, -1);

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

			float3 collisionNormalSample;
			collisionNormalSample.xy = CollisionNormal[cellTexID] * 2.0 - 1.0;
			collisionNormalSample.z = -sqrt(saturate(1.0 - dot(collisionNormalSample.xy, collisionNormalSample.xy)));

			collision += collisionSample * w;
			collisionNormal += collisionNormalSample * ComputeCollisionAmount(worldPosition, collisionSample) * w;

			wsum += w;
		}
		
		if (wsum > 0){
			collision /= wsum;
		} else {
			collision = 1000000;
		}
		
		collisionNormal = normalize(collisionNormal);
	}

	float3 GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position, 1.0)).xyz;

		if (input.Color.w > 0.0) {
			float2 collision;
			float3 collisionNormal;
			GetCollision(worldPosition, collision, collisionNormal);

			float3 displacement = collisionNormal;
			displacement.xyz *= ComputeCollisionAmountElastic(worldPosition, collision);

			// Bend grass downwards
			displacement.z -= length(displacement.xy);

			// Scale grass by mesh
			float bendability = max(1.0, input.Position.z + 1.0) * 0.01;
			displacement.xyz *= bendability;

			// Scale grass by physical height
			float scaledHeight = input.Position.z * (input.InstanceData4.y * ScaleMask.z + 1.0);
			float bendabilityZ = max(1.0, scaledHeight + 1.0) * 0.005;

			displacement.z -= ComputeCollisionAmount(worldPosition, collision) * bendabilityZ;
			
			// Scale grass by wind amount (detect rocks and bottom of grass)
			float alpha = saturate(input.Color.w * 10.0); 

			return displacement * alpha;			
		}

		return 0.0;
	}
}