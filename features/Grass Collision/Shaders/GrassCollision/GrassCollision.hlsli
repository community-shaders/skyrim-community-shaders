namespace GrassCollision
{
	Texture2D<float2> Collision : register(t100);
	SamplerState CollisionSampler : register(s0);  // Bilinear sampler

	struct CollisionData
	{
		float4 centre[2];
	};

	cbuffer GrassCollisionPerFrame : register(b5)
	{
		float2 PosOffset;  // cell origin in camera model space
		uint2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

		float timeDelta;
		float3 eyePosition;

		CollisionData collisionData[256];
		uint numCollisions;
		uint pad0[3];
	}

	const static uint2 ARRAY_DIM = uint2(1024, 1024);
	const static float2 ARRAY_SIZE = float2(2048.0, 2048.0);
	const static float2 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;

	float2 SampleCollision(float2 worldPosMS)
	{
		// Convert model space position to clipmap coordinates (similar to Skylighting)
		float2 positionMSAdjusted = worldPosMS - PosOffset;
		float2 uv = positionMSAdjusted / ARRAY_SIZE + 0.5;

		// Check bounds
		if (any(uv < 0) || any(uv > 1))
			return float2(100000000, 0);

		// Sample at mip level 0 (vertex shader requires explicit mip level)
		return Collision.SampleLevel(CollisionSampler, uv, 0);
	}

	// Compute surface normal from Collision texture
	float3 GetCollisionNormal(float2 worldPosMS, int pixelOffset = 1)
	{
		// Convert model space position to clipmap coordinates
		float2 positionMSAdjusted = worldPosMS - PosOffset;
		float2 uv = positionMSAdjusted / ARRAY_SIZE + 0.5;

		// Check bounds
		if (any(uv < 0) || any(uv > 1))
			return float3(0, 0, 1);

		// Fetch texel size
		float2 texelSize = 1.0 / ARRAY_DIM;

		// Sample with offsets in texture space
		float heightLeft = Collision.SampleLevel(CollisionSampler, uv + float2(-texelSize.x * pixelOffset, 0), 0).x;
		float heightRight = Collision.SampleLevel(CollisionSampler, uv + float2(texelSize.x * pixelOffset, 0), 0).x;
		float heightDown = Collision.SampleLevel(CollisionSampler, uv + float2(0, -texelSize.y * pixelOffset), 0).x;
		float heightUp = Collision.SampleLevel(CollisionSampler, uv + float2(0, texelSize.y * pixelOffset), 0).x;

		// Build tangent vectors in XY plane
		float3 tangentX = float3(1, 0, heightRight - heightLeft);
		float3 tangentY = float3(0, 1, heightUp - heightDown);

		// Cross product gives the surface normal
		return normalize(cross(tangentX, tangentY));
	}

	float ease_in_out_elastic(float x) {
		float t = x; float b = 0; float c = 1; float d = 1;
		float s=1.70158;float p=0;float a=c;
		if (t==0) return b;  if ((t/=d/2)==2) return b+c;  if (p ==0) p=d*(.3*1.5);
		if (a < abs(c)) { a=c; s=p/4; }
		else s = p/(2*3.14159265359) * asin (c/a);
		if (t < 1) return -.5*(a*pow(2,10*(t-=1)) * sin( (t*d-s)*(2*3.14159265359)/p )) + b;
		return a*pow(2,-10*(t-=1)) * sin( (t*d-s)*(2*3.14159265359)/p )*.5 + c + b;
	}

	float3 GetDisplacedPosition(VS_INPUT input, float3 position, uint eyeIndex = 0)
	{
		float3 worldPosition = mul(World[eyeIndex], float4(position, 1.0)).xyz;

		// Convert to model space (camera-relative) for clipmap sampling
		float2 worldPosMS = worldPosition.xy;

		if (input.Color.w > 0.0) {
			float2 collision = SampleCollision(worldPosMS);

			// Check for valid collision (not out of bounds)
			if (collision.x < 100000000) {
				// Check if collision is below grass vertex
				if (collision.x < worldPosition.z) {
					float displacementAmount = max(0, worldPosition.z - collision.x);

					float3 displacementNormal = -GetCollisionNormal(worldPosMS);
					displacementNormal.z = -abs(displacementNormal.z);

					float timeSince = ease_in_out_elastic(
						saturate((collision.y - (collision.x + 1)) / max(collision.x - (collision.x + 1), 0.001))
					);

					float3 displacement = displacementNormal * displacementAmount * timeSince * 0.5;

					return displacement;
				}
			}
		}

		return 0.0;
	}
}