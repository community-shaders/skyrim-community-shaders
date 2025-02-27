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

	void SmoothLimitDisplacement(inout float3 displacement, float maxLength)
	{
		float lengthSq = dot(displacement, displacement);
		float maxLengthSq = maxLength * maxLength;

		if (lengthSq > maxLengthSq) {
			float length = sqrt(lengthSq);
			float smoothFactor = smoothstep(0.0, maxLength, length);
			float scale = lerp(1.0, maxLength / length, smoothFactor);

			displacement *= scale;
		}
	}

	float3 GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position, 1.0)).xyz;
		float alpha = saturate(input.Color.w * 10.0);

		if (length(worldPosition) < 2048.0 && alpha > 0.0) {
			float3 displacement = 0.0;

			for (uint i = 0; i < numCollisions; i++) {
				float dist = distance(collisionData[i].centre[eyeIndex].xyz, worldPosition);
				float power = 1.0 - saturate(dist / collisionData[i].centre[0].w);
				float3 direction = worldPosition - collisionData[i].centre[eyeIndex].xyz;
				float3 shift = power * power * direction;
				shift.xy *= 1.0 + saturate(shift.z);
				shift.z = min(shift.z, 0);
				displacement += shift;
				displacement -= length(shift);
			}

			displacement *= saturate((position.z - input.InstanceData1.z) * 0.1) * alpha;

			SmoothLimitDisplacement(displacement, 32.0);

			return displacement;
		}

		return 0.0;
	}
}
