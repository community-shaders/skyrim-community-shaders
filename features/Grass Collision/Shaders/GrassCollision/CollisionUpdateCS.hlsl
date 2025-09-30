
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

Texture2D<float2> previousCollision : register(t0);

RWTexture2D<float2> currentCollision : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID)
{
	const uint2 ARRAY_DIM = uint2(1024, 1024);
	const float2 ARRAY_SIZE = float2(2048.0, 2048.0);

	uint2 cellID = (uint2)max(int2(dtid.xy) - (int2)ArrayOrigin, 0) % ARRAY_DIM;

	float2 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
	cellCentreMS = cellCentreMS / ARRAY_DIM * ARRAY_SIZE + PosOffset.xy;

	float2 lowestHeight = 100000000;

	uint2 prevTexID = (cellID + ArrayOrigin) % ARRAY_DIM;
	float2 previousCollisionSample = previousCollision[prevTexID];

	// Apply temporal decay
	// if (previousCollisionSample.x < 100000000) {
	// 	lowestHeight = previousCollisionSample;
	// 	lowestHeight.y += timeDelta;

	// 	// Apply camera height change
	// 	lowestHeight.x -= FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPreviousPosAdjust[0].z;

	// 	// Fade out over time
	// 	float timeSince = saturate((lowestHeight.y - (lowestHeight.x + 1)) / max(lowestHeight.x - (lowestHeight.x + 1), 0.001));

	// 	if (timeSince == 0.0)
	// 		lowestHeight = 100000000;
	// }

	// Process collision data
	for (uint i = 0; i < numCollisions; i++) {
		float radius = collisionData[i].centre[0].w;
		collisionData[i].centre[0].xyz -= eyePosition;

		// Get the lowest point of the sphere at this cell position
		float dist = distance(collisionData[i].centre[0].xy, cellCentreMS);

		// Only process if we're within the sphere's radius
		if (dist < radius) {
			// Get sphere geometry
			float heightFromCenter = sqrt(radius * radius - dist * dist);
			float height = collisionData[i].centre[0].z - heightFromCenter;

			if (height <= lowestHeight.x) {
				lowestHeight = height;
				lowestHeight = -1000;
			}
		}
	}

	currentCollision[dtid.xy] = lowestHeight;
}