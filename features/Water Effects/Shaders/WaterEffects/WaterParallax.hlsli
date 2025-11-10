namespace WaterEffects
{
	// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
	// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

	// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
	// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
	// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

	float GetMipLevel(float2 coords, Texture2D<float4> tex)
	{
		// Compute the current gradients:
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);

#if defined(VR)
		textureDims /= 16.0;
#else
		textureDims /= 8.0;
#endif

		float2 texCoordsPerSize = coords * textureDims;

		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		// Find min of change in u and v across quad: compute du and dv magnitude across quad
		float2 dTexCoords = dxSize * dxSize + dySize * dySize;

		// Standard mipmapping uses max here
		float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);

		// Compute the current mip level  (* 0.5 is effectively computing a square root before )
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

		return mipLevel;
	}

	float GetHeight(PS_INPUT input, float2 currentOffset, float3 normalScalesRcp, float3 mipLevels)
	{
		float3 heights;
		heights.x = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevels.x).w;
		heights.y = Normals02Tex.SampleLevel(Normals02Sampler, input.TexCoord1.zw + currentOffset * normalScalesRcp.y, mipLevels.y).w;
		heights.z = Normals03Tex.SampleLevel(Normals03Sampler, input.TexCoord2.xy + currentOffset * normalScalesRcp.z, mipLevels.z).w;
		heights *= NormalsAmplitude.xyz;
		return 1.0 - (heights.x + heights.y + heights.z);
	}

	float2 GetParallaxOffset(PS_INPUT input, float3 normalScalesRcp)
	{
		float3 viewDirection = normalize(input.WPosition.xyz);
		float2 parallaxOffsetTS = viewDirection.xy / -viewDirection.z;

		// Parallax scale is also multiplied by normalScalesRcp
		parallaxOffsetTS *= 5.0;

		float3 mipLevels;
		mipLevels.x = GetMipLevel(input.TexCoord1.xy, Normals01Tex);
		mipLevels.y = GetMipLevel(input.TexCoord1.zw, Normals02Tex);
		mipLevels.z = GetMipLevel(input.TexCoord2.xy, Normals03Tex);

#if defined(VR)
		mipLevels = mipLevels + 4;
#else
		mipLevels = mipLevels + 3;
#endif

		float stepSize = rcp(16.0);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] while (currHeight > currBound)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetHeight(input, currBound * parallaxOffsetTS.xy, normalScalesRcp, mipLevels);
		}

		float prevBound = currBound - stepSize;

		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		float denominator = delta2 - delta1;
		float parallaxAmount = (currBound * delta2 - prevBound * delta1) / denominator;

		return parallaxOffsetTS.xy * parallaxAmount;
	}

	// Sphere tracing using distance-based ray marching (GPU Gems 2, Chapter 8)
	// Adaptively steps through the heightfield based on safe distance from surface
	float2 GetFlowmapParallaxOffset(PS_INPUT input, float2 baseUV)
	{
		float3 viewDirection = normalize(input.WPosition.xyz);
		float2 rayDir = viewDirection.xy / -viewDirection.z;
		
		// Normalize ray direction and apply parallax scale
		float rayLength = length(rayDir);
		rayDir = normalize(rayDir) * 0.08;  // Scale for flowmap UV space
		
		// Calculate base mip level from UV gradients
		float2 textureDims;
		FlowMapNormalsTex.GetDimensions(textureDims.x, textureDims.y);
		float2 dx = ddx(baseUV * textureDims);
		float2 dy = ddy(baseUV * textureDims);
		float delta = max(dot(dx, dx), dot(dy, dy));
		float baseMipLevel = 0.5 * log2(max(delta, 0.00001)) + SharedData::MipBias;
		baseMipLevel = clamp(baseMipLevel, 0.0, 5.0);
		
		// Sphere tracing: march along ray using distance field
		float2 currentUV = baseUV;
		float currentHeight = 1.0;  // Start at maximum height
		float distanceTraveled = 0.0;
		const int maxSteps = 12;
		const float minStepSize = 0.001;
		
		[unroll]
		for (int i = 0; i < maxSteps; i++)
		{
			// Sample height from texture (use alpha + normal hybrid)
			float4 texSample = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, currentUV, baseMipLevel);
			float3 decodedNormal = normalize(texSample.xyz * 2.0 - 1.0);
			
			// Hybrid height: blend alpha (70%) and normal.z (30%) for stability
			float heightFromAlpha = texSample.a;
			float heightFromNormal = decodedNormal.z;
			float surfaceHeight = lerp(heightFromNormal, heightFromAlpha, 0.7);
			
			// Calculate distance to surface (how far we are above/below surface)
			float heightDifference = currentHeight - surfaceHeight;
			
			// If we're below the surface, we've intersected - stop marching
			if (heightDifference < 0.0)
				break;
			
			// Safe step distance based on height above surface
			// Scale by heightDifference to take larger steps when far from surface
			float stepDistance = max(heightDifference * 0.5, minStepSize);
			
			// March along ray by safe distance
			currentUV += rayDir * stepDistance;
			distanceTraveled += stepDistance;
			currentHeight -= stepDistance;
			
			// Safety: don't march too far
			if (distanceTraveled > 1.0 || currentHeight < 0.0)
				break;
		}
		
		// Return the offset from original UV
		return currentUV - baseUV;
	}
}
