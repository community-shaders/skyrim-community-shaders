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

	void ClampDisplacement(inout float3 displacement, float maxLength)
	{
		float lengthSq = displacement.x * displacement.x +
		                 displacement.y * displacement.y +
		                 displacement.z * displacement.z;

		if (lengthSq > maxLength * maxLength)  // Compare squared values for performance
		{
			float length = sqrt(lengthSq);
			float scale = maxLength / length;

			displacement.x *= scale;
			displacement.y *= scale;
			displacement.z *= scale;
		}
	}

	float3 GetDisplacedPosition(float3 position, float alpha, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position, 1.0)).xyz;

		if (length(worldPosition) < 2048.0 && alpha > 0.0) {
			float3 displacement = 0.0;

			for (uint i = 0; i < numCollisions; i++) {
				float dist = distance(collisionData[i].centre[eyeIndex].xyz, worldPosition);
				float power = 1.0 - saturate(dist / collisionData[i].centre[0].w);
				float3 direction = worldPosition - collisionData[i].centre[eyeIndex].xyz;
				float3 shift = power * power * direction;
				displacement += shift;
			}

			ClampDisplacement(displacement, 10);
			return displacement * saturate(alpha * 10);
		}

		return 0.0;
	}
}
