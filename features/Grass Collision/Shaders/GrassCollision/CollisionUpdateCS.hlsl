
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

struct CollisionData
{
	float4 centre[2];
};

cbuffer PerFrameCB : register(b0)
{
	float2 PosOffset;  // cell origin in camera space
	uint2 ArrayOrigin; // xy: array origin (clipmap wrapping)
	
	int2 ValidMargin;
	float TimeDelta;
	uint numCollisions;

	CollisionData collisionData[256];
}

RWTexture2D<float2> Collision : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID)
{
	const uint TEXTURE_SIZE = 512;
	const float WORLD_SIZE = 4096;
	float2 ZRANGE = float2(1024.0, -1024.0);

	uint2 cellID = uint2(max(int2(dtid.xy) - ArrayOrigin, 0) % TEXTURE_SIZE);

	float2 cellCentreMS = cellID + 0.5 - TEXTURE_SIZE / 2;
	cellCentreMS = cellCentreMS / TEXTURE_SIZE * WORLD_SIZE + PosOffset.xy;

	uint2 validMin = (uint2)max(0, ValidMargin.xy);
	uint2 validMax = TEXTURE_SIZE - 1 + (uint2)min(0, ValidMargin.xy);
	bool isValid = all(cellID >= validMin) && all(cellID <= validMax);  // check if the cell is newly added

	float2 collision = max(ZRANGE.x, ZRANGE.y);
	float2 previousCollision = collision;

	float2 fadeRate = TimeDelta * 50 * float2(0.5, 1.0);

	if (isValid) {
		previousCollision = Collision[dtid.xy];
		previousCollision = lerp(ZRANGE.x, ZRANGE.y, previousCollision);

		// Apply camera height change
		previousCollision -= FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPreviousPosAdjust[0].z;

		// Temporal decay
		previousCollision += fadeRate;

		collision = previousCollision;
	}

	// Process collision data
	for (uint i = 0; i < numCollisions; i++) {
		float radius = collisionData[i].centre[0].w * 2.0;
		float3 colliderCentreMS = collisionData[i].centre[0].xyz - FrameBuffer::CameraPosAdjust[0].xyz;

		// Get the lowest point of the sphere at this cell position
		float dist = distance(colliderCentreMS.xy, cellCentreMS);

		// Only process if we're within the sphere's radius
		if (dist < radius) {
			// Get sphere geometry approximation (diamond shape)
			float heightFromCenter = radius - dist;
			float height = colliderCentreMS.z - heightFromCenter;

			collision.x = min(previousCollision.x, height);

			if (height < collision.y) {
				collision.y = height;
			}
		}
	}

	collision = (collision - ZRANGE.x) / (ZRANGE.y - ZRANGE.x);

	Collision[dtid.xy] = collision;
}