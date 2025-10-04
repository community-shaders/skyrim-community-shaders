namespace GrassCollision
{
	Texture2D<float4> Collision : register(t100);

	cbuffer GrassCollisionPerFrame : register(b5)
	{
		float2 PosOffset;  // cell origin in camera space
		uint2 ArrayOrigin; // xy: array origin (clipmap wrapping)

		int2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;
	}

	const static uint TEXTURE_SIZE = 512;
	const static float WORLD_SIZE = 4096;
	const static float CELL_SIZE = WORLD_SIZE / TEXTURE_SIZE;
	const static float2 ZRANGE = float2(2048.0, -2048.0);

	float ProceduralAnimation(float x) {
		float fadeRate = 100;
		x /= fadeRate;
		float frequency = 4 * Math::PI;
		return cos(x * frequency) * exp(-x * 4);
	}

	void GetCollision(float3 worldPosition, out float2 collisionHeights, out float collisionAmount, out float2 previousCollisionHeights, out float previousCollisionAmount)
	{
		float2 positionMSAdjusted = worldPosition - PosOffset.xy;
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
			for (int j = 0; j < 2; j++)
		{
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
			collisionAmount += max(0, worldPosition.z - collisionSample.x) * ProceduralAnimation(collisionSample.y - collisionSample.x) * w;

			previousCollisionHeights += collisionSample.z * w;
			previousCollisionAmount += max(0, worldPosition.z - collisionSample.z) * ProceduralAnimation(collisionSample.w - collisionSample.z) * w;

			wsum += w;
		}

		if (wsum > 0.0){
			collisionHeights /= wsum;
			collisionAmount /= wsum;
			previousCollisionHeights /= wsum;
			previousCollisionAmount /= wsum;
		} else {
			collisionHeights = TEXTURE_SIZE;
			collisionAmount = 0.0;
			previousCollisionHeights = TEXTURE_SIZE;
			previousCollisionAmount = 0.0;
		}

	}

	float3 ComputeNormalFromHeights(float h0, float hX, float hY, float delta)
	{
		float3 tangentX = float3(delta, 0, hX - h0);
		float3 tangentY = float3(0, delta, hY - h0);
		return -normalize(cross(tangentX, tangentY) * float3(1.0, 1.0, 0.1));
	}

	void ComputeCollision(float3 worldPosition, float delta, out float3 collision, out float3 previousCollision)
	{
		// Sample collision at three points forming a small triangle
		float2 collisionCenter;
		float2 collisionX;
		float2 collisionY;

		float collisionCenterAmount;
		float collisionXAmount;
		float collisionYAmount;

		float2 previousCollisionCenter;
		float2 previousCollisionX;
		float2 previousCollisionY;

		float previousCollisionCenterAmount;
		float previousCollisionXAmount;
		float previousCollisionYAmount;

		GetCollision(worldPosition + float3(-delta, -delta, 0), collisionCenter, collisionCenterAmount, previousCollisionCenter, previousCollisionCenterAmount);
		GetCollision(worldPosition + float3(delta, 0, 0), collisionX, collisionXAmount, previousCollisionX, previousCollisionXAmount);
		GetCollision(worldPosition + float3(0, delta, 0), collisionY, collisionYAmount, previousCollisionY, previousCollisionYAmount);

		// Process current collision
		float3 currentAmounts = float3(collisionCenterAmount, collisionXAmount, collisionYAmount);
		float avgCurrentAmount = dot(currentAmounts, 1.0 / 3.0);
		collision = ComputeNormalFromHeights(collisionCenter.x, collisionX.x, collisionY.x, delta) * avgCurrentAmount;

		// Process previous collision
		float3 previousAmounts = float3(previousCollisionCenterAmount, previousCollisionXAmount, previousCollisionYAmount);
		float avgPreviousAmount = dot(previousAmounts, 1.0 / 3.0);
		previousCollision = ComputeNormalFromHeights(previousCollisionCenter.x, previousCollisionX.x, previousCollisionY.x, delta) * avgPreviousAmount;
	}

	void GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex, out float3 displacement, out float3 previousDisplacement)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position.xyz, 1.0)).xyz;

		if (input.Color.w > 0.0 && length(worldPosition) < 2048.0) {
			float3 worldPositionCentre = mul(World[eyeIndex], float4(input.InstanceData1.xyz, 1.0)).xyz;

			// Limit stretching
			float3 remappedWorldPosition = lerp(worldPosition, worldPositionCentre, float3(0.95, 0.95, 0.5));

			// Return base collision
			float3 collision, previousCollision;
			ComputeCollision(remappedWorldPosition, CELL_SIZE, collision, previousCollision);

			// Scale grass by wind amount (detect rocks and bottom of some grass)
			float alpha = saturate(input.Color.w * 10.0);

			displacement = collision * alpha;
			previousDisplacement  = previousCollision * alpha;
		} else {
			displacement = 0.0;
			previousDisplacement = 0.0;
		}
	}
}