namespace GrassCollision
{
	struct CollisionData
	{
		float4 centre[2];
	};

	cbuffer GrassCollisionPerFrame : register(b5)
	{
		CollisionData collisionData[256];
		uint numCollisions;
	}

	float3 GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position, 1.0)).xyz;

		if (length(worldPosition) < 2048.0 && input.Color.w > 0.0) {
			float lowestHeight = worldPosition.z;
			float3 lowestDisplacement = float3(0.0, 0.0, 0.0);

			for (uint i = 0; i < numCollisions; i++) {
				float radius = collisionData[i].centre[0].w;

				// Get the lowest point of the sphere at WorldPosition.xy
				float dist = distance(collisionData[i].centre[eyeIndex].xy, worldPosition.xy);

				// Only process if we're within the sphere's radius
				if (dist < radius) {
					// Get sphere geometry
					float heightFromCenter = sqrt(max(0.0, radius * radius - dist * dist));
					float height = collisionData[i].centre[eyeIndex].z - heightFromCenter;

					// Add weighted displacement direction
					lowestDisplacement += (worldPosition.xyz - collisionData[i].centre[eyeIndex].xyz) * max(0, worldPosition.z - height);

					// Set lowest point
					lowestHeight = min(lowestHeight, height);
				}
			}

			// Check for valid collision
			if (lowestHeight < worldPosition.z){
				float3 displacementNormal = normalize(lowestDisplacement);
				displacementNormal.z = -abs(displacementNormal.z);

				float displacementAmount = max(0, worldPosition.z - lowestHeight);

				float scaledHeight = input.Position.z * (input.InstanceData4.y * ScaleMask.z + 1.0);
				float bendability = saturate(input.Color.w * 10.0) * max(0.0, scaledHeight) * 0.005;

				return displacementNormal * displacementAmount * bendability;
			}

		}

		return 0.0;
	}
}
