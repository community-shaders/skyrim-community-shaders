
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

struct CollisionData
{
	float4 centre[2];
};

cbuffer PerFrameCB : register(b0)
{
	float2 currentCenter;
	float2 previousCenter;

	float worldSize;
	float timeDelta;
	float2 pad;

	uint2 textureDimensions;
	int2 texelOffset;   
		
	CollisionData collisionData[256];
	uint numCollisions;
	uint pad0[3];
}

Texture2D<float2> previousCollision : register(t0);

RWTexture2D<float2> currentCollision : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)] void main(
	uint3 groupId
	: SV_GroupID, uint3 dispatchThreadId
	: SV_DispatchThreadID, uint3 groupThreadId
	: SV_GroupThreadID, uint groupIndex
	: SV_GroupIndex) {
	const float textureSize = 4096;

	float2 uv = float2(dispatchThreadId.xy + 0.5) / textureSize;

	float2 worldPosition = (uv * 2.0 - 1.0) * 2048.0;

	float2 eyePositionDelta = FrameBuffer::CameraPosAdjust[0].xyz - FrameBuffer::CameraPreviousPosAdjust[0].xyz;

	float2 previousUv = worldPosition + eyePositionDelta.xy;
	previousUv = (previousUv / 2048.0) * 0.5 + 0.5;

	float2 lowestHeight = 100000000;
	float2 displacementNormal = float2(0, 0);

	if (saturate(previousUv.x) == previousUv.x && saturate(previousUv.y) == previousUv.y) {
		float4 previousCollisionSample = previousCollision.SampleLevel(LinearSampler, previousUv, 0);

		lowestHeight = previousCollisionSample.xy;

		lowestHeight.y += timeDelta;

		lowestHeight.xy -= FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPreviousPosAdjust[0].z;

		float timeSince = (
			saturate((lowestHeight.y - (lowestHeight.x + 2)) / (lowestHeight.x - (lowestHeight.x + 2)))
		);

		if (timeSince == 0.0)
			lowestHeight.xy = 100000000;
					
		displacementNormal = normalize(previousCollisionSample.zw);
	}

	for (uint i = 0; i < numCollisions; i++) {
		float radius = collisionData[i].centre[0].w;
		
		// Get the lowest point of the sphere at WorldPosition.xy
		float dist = distance(collisionData[i].centre[0].xy, worldPosition.xy);
		
		// Only process if we're within the sphere's radius
		if (dist < radius) {
			// Get sphere geometry
			float heightFromCenter = sqrt(radius * radius - dist * dist);
			float height = collisionData[i].centre[0].z - heightFromCenter;
			
			if (height <= lowestHeight.x || height <= lowestHeight.y){
				lowestHeight = height; 
			}
		}
	}

	currentCollision[dispatchThreadId.xy] = float4(lowestHeight, displacementNormal);
}