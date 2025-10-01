
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
RWTexture2D<float2> CollisionNormal : register(u1);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID)
{
	const uint2 ARRAY_DIM = uint2(1024, 1024);
	const float2 ARRAY_SIZE = float2(4096.0, 4096.0);

	uint2 cellID = uint2(max(int2(dtid.xy) - ArrayOrigin, 0) % ARRAY_DIM);

	float2 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
	cellCentreMS = cellCentreMS / ARRAY_DIM * ARRAY_SIZE + PosOffset.xy;

	bool isValid = true;

	float2 collision = 100000000;
	float3 collisionNormal = float3(0, 0, -1);

	float2 previousCollision = 100000000;
	float3 previousCollisionNormal = float3(0, 0, -1);

	if (isValid) {
		previousCollision = Collision[dtid.xy];

		previousCollisionNormal.xy = CollisionNormal[dtid.xy] * 2.0 - 1.0;
		// Recompute Z
		previousCollisionNormal.z = sqrt(saturate(1.0 - dot(previousCollisionNormal.xy, previousCollisionNormal.xy)));

		// Apply camera height change
		previousCollision -= FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPreviousPosAdjust[0].z;

		// Temporal decay
		previousCollision.y += timeDelta;

		float collisionAmount = saturate((previousCollision.x + 2.0 - previousCollision.y) / 2.0);

		if (collisionAmount == 0.0){
			previousCollision = 100000000;
			previousCollisionNormal = float3(0, 0, -1);
		}

		collision = previousCollision;
		collisionNormal = previousCollisionNormal;
	}

	// Process collision data
	for (uint i = 0; i < numCollisions; i++) {
		float radius = collisionData[i].centre[0].w;
		float3 colliderCentreMS = collisionData[i].centre[0].xyz - eyePosition.xyz;

		// Get the lowest point of the sphere at this cell position
		float dist = distance(colliderCentreMS.xy, cellCentreMS);

		// Only process if we're within the sphere's radius
		if (dist < radius) {
			// Get sphere geometry
			float heightFromCenter = sqrt(radius * radius - dist * dist);
			float height = colliderCentreMS.z - heightFromCenter;
			if (height < collision.x || height < collision.y) {
				collision = height;

				// Get normal of sphere
				// Collision point on the sphere surface
				float3 collisionPoint = float3(cellCentreMS.xy, height);

				// Normal is the direction from sphere center to surface point
				collisionNormal = collisionPoint - colliderCentreMS;
			}
		}
	}

	Collision[dtid.xy] = collision;
	CollisionNormal[dtid.xy] = normalize(collisionNormal) * 0.5 + 0.5;
}