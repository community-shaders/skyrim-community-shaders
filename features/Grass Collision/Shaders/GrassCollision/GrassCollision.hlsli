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

		float CameraHeightDelta;
	}

	const static uint TEXTURE_SIZE = 512;
	const static float WORLD_SIZE = 4096;
	const static float CELL_SIZE = WORLD_SIZE / TEXTURE_SIZE;
	const static float2 ZRANGE = float2(2048.0, -2048.0);

	float ProceduralAnimation(float x, float distanceFromCenter) {
		float fadeRate = 250;
		x /= fadeRate;
		x /= distanceFromCenter;
		x *= 100;
		float frequency = 4 * Math::PI;
		return cos(x * frequency) * exp(-x * 4);
	}

	void GetCollision(float3 worldPosition, float maximumDepth, float distanceFromCenter, out float collisionHeights, out float collisionAmount, out float previousCollisionHeights, out float previousCollisionAmount)
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
			collisionAmount += max(0, worldPosition.z - collisionSample.x) * ProceduralAnimation(collisionSample.y - collisionSample.x, distanceFromCenter) * w;
			collisionAmount = min(collisionAmount, maximumDepth);

			previousCollisionHeights += collisionSample.z * w;
			previousCollisionAmount += max(0, worldPosition.z - collisionSample.z) * ProceduralAnimation(collisionSample.w - collisionSample.z, distanceFromCenter) * w;
			previousCollisionAmount = min(previousCollisionAmount, maximumDepth);

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
		float3 crossProd = cross(tangentX, tangentY) * float3(1.0, 1.0, 0.1);

		float lenSq = dot(crossProd, crossProd);
		return lenSq > 1e-12 ? -crossProd * rsqrt(lenSq) : float3(0, 0, -1);
	}

	void ComputeCollision(float3 worldPosition, float maximumDepth, float distanceFromCenter, float delta, out float3 collision, out float3 previousCollision)
	{
		// Sample collision at three points forming a small triangle
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

		GetCollision(worldPosition + float3(-delta, -delta, 0), maximumDepth, distanceFromCenter, collisionCenter, collisionCenterAmount, previousCollisionCenter, previousCollisionCenterAmount);
		GetCollision(worldPosition + float3(delta, 0, 0), maximumDepth, distanceFromCenter, collisionX, collisionXAmount, previousCollisionX, previousCollisionXAmount);
		GetCollision(worldPosition + float3(0, delta, 0), maximumDepth, distanceFromCenter, collisionY, collisionYAmount, previousCollisionY, previousCollisionYAmount);

		// Process current collision
		float3 currentAmounts = float3(collisionCenterAmount, collisionXAmount, collisionYAmount);
		float avgCurrentAmount = dot(currentAmounts, float3(1.0, 1.0, 1.0)) / 3.0;
		collision = ComputeNormalFromHeights(collisionCenter, collisionX, collisionY, delta) * avgCurrentAmount;

		// Process previous collision
		float3 previousAmounts = float3(previousCollisionCenterAmount, previousCollisionXAmount, previousCollisionYAmount);
		float avgPreviousAmount = dot(previousAmounts, float3(1.0, 1.0, 1.0)) / 3.0;
		previousCollision = ComputeNormalFromHeights(previousCollisionCenter, previousCollisionX, previousCollisionY, delta) * avgPreviousAmount;
	}

	void GetDisplacedPosition(VS_INPUT input, float3 position, out float3 displacement, out float3 previousDisplacement)
	{
		float3 worldPosition = mul(World[0], float4(position.xyz, 1.0)).xyz;
		float nearFactor = smoothstep(2048.0, 0.0, length(worldPosition));

		if (input.Color.w > 0.0 && nearFactor > 0.0) {
			float3 worldPositionCentre = mul(World[0], float4(input.InstanceData1.xyz, 1.0)).xyz;

			// Limit stretching
			float3 remappedWorldPosition = lerp(worldPosition, worldPositionCentre, float3(0.95, 0.95, 0.0));

			float distanceFromCenter = length(worldPosition - worldPositionCentre) + 0.01;
			float maximumDepth = max(0.0, worldPosition.z - worldPositionCentre.z);

			// Return base collision
			float3 collision, previousCollision;
			ComputeCollision(remappedWorldPosition, maximumDepth, distanceFromCenter, CELL_SIZE, collision, previousCollision);

			// Do not let collision move upwards
			collision.z = -abs(collision.z);
			previousCollision.z = -abs(previousCollision.z);

			// Scale grass by wind amount (detect rocks and bottom of some grass)
			float alpha = saturate(input.Color.w * 10.0);

			displacement = collision * alpha * nearFactor * 0.75;
			previousDisplacement  = previousCollision * alpha * nearFactor * 0.75;
		} else {
			displacement = 0.0;
			previousDisplacement = 0.0;
		}
	}
}