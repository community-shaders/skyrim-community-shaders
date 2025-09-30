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
		float2 currentCenter;
		float2 previousCenter;

		float worldSize;
		float timeDelta;
		float2 pad;

		uint2 textureDimensions;
		int2 texelOffset;   
		
		CollisionData collisionData[256];
		uint numCollisions;
	}
	
	float4 SampleCollision(float2 worldPosXY)
	{
		// Convert world position to UV [0, 1]
		float2 uv = (worldPosXY / 2048.0) * 0.5 + 0.5;
		
		// Sample at mip level 0 (vertex shader requires explicit mip level)
		return Collision.SampleLevel(CollisionSampler, uv, 0);
	}

	// Compute surface normal from Collision texture
    float3 GetCollisionNormal(float2 worldPosXY, int pixelOffset = 1)
    {
        // Convert world position to UV
        float2 uv = (worldPosXY / 2048.0) * 0.5 + 0.5;

        // Fetch texel size (inverse of textureDimensions, supplied in cbuffer)
        float2 texelSize = 1.0 / textureDimensions;

        // Sample with offsets in texture space
        float heightLeft  = Collision.SampleLevel(CollisionSampler, uv + float2(-texelSize.x * pixelOffset, 0), 0).y;
        float heightRight = Collision.SampleLevel(CollisionSampler, uv + float2( texelSize.x * pixelOffset, 0), 0).y;
        float heightDown  = Collision.SampleLevel(CollisionSampler, uv + float2(0, -texelSize.y * pixelOffset), 0).y;
        float heightUp    = Collision.SampleLevel(CollisionSampler, uv + float2(0,  texelSize.y * pixelOffset), 0).y;

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
		float2 uv = (worldPosition.xy / 2048.0) * 0.5 + 0.5;

		if (saturate(uv.x) == uv.x && saturate(uv.y) == uv.y) {
			if (input.Color.w > 0.0){
				float lowestHeight = worldPosition.z;

				float4 collision = SampleCollision(worldPosition.xy);

				// Check for valid collision
				if (collision.y < worldPosition.z)
				{
					float displacementAmount = max(0, worldPosition.z - collision.x);

					float3 displacementNormal = -GetCollisionNormal(worldPosition.xy);
					displacementNormal.z = -abs(displacementNormal.z);

					float timeSince = ease_in_out_elastic(
						saturate((collision.y - (collision.x + 2)) / (collision.x - (collision.x + 2)))
					);
					
					float3 displacement = displacementNormal * displacementAmount * timeSince * 0.5;

					return displacement;
				}
			}
		}

		return 0.0;
	}
}