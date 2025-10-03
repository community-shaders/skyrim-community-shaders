
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

struct CollisionData
{
	float4 centre[2];
};

cbuffer PerFrameCB : register(b0)
{
	float2 PosOffset;  // cell origin in camera model space
	uint2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

	float timeDelta;
	float3 eyePosition;

	CollisionData collisionData[256];
	uint numCollisions;
	uint pad0[3];
}

RWTexture2D<float2> Collision : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID)
{
	const uint2 ARRAY_DIM = uint2(512, 512);
	const float2 ARRAY_SIZE = float2(4096.0, 4096.0);
	float2 ZRANGE = float2(1024.0, -1024.0);

	uint2 cellID = uint2(max(int2(dtid.xy) - ArrayOrigin, 0) % ARRAY_DIM);

	float2 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
	cellCentreMS = cellCentreMS / ARRAY_DIM * ARRAY_SIZE + PosOffset.xy;

	bool isValid = true;

	float2 collision = ARRAY_SIZE.x * 0.5;
	float2 previousCollision = ARRAY_SIZE.x * 0.5;

	float fadeRate = timeDelta * 10;

	if (isValid) {
		previousCollision = Collision[dtid.xy];
		previousCollision = lerp(ZRANGE.x, ZRANGE.y, previousCollision);

		// Apply camera height change
		previousCollision -= FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPreviousPosAdjust[0].z;

		// Temporal decay
		previousCollision.x += fadeRate * 0.5;
		previousCollision.y += fadeRate;

		collision = previousCollision;
	}

	// Process collision data
	for (uint i = 0; i < numCollisions; i++) {
		float radius = collisionData[i].centre[0].w * 1.5;
		float3 colliderCentreMS = collisionData[i].centre[0].xyz - eyePosition.xyz;

		// Get the lowest point of the sphere at this cell position
		float dist = distance(colliderCentreMS.xy, cellCentreMS);

		// Only process if we're within the sphere's radius
		if (dist < radius) {
			// Get sphere geometry
			float heightFromCenter = (radius - dist);
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