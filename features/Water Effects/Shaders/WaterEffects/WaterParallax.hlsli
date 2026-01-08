namespace WaterEffects
{
	// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
	// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

	// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
	// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
	// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

float GetMipLevel(float2 coords, Texture2D<float4> tex)
{
    // Get actual texture dimensions
    float2 actualTextureDims;
    tex.GetDimensions(actualTextureDims.x, actualTextureDims.y);
    
    // Use hardcoded 512x512 for mip calculation
    float2 textureDims = float2(512.0, 512.0);

    float2 texCoordsPerSize = coords;
    
    float2 dxSize = ddx(texCoordsPerSize);
    float2 dySize = ddy(texCoordsPerSize);
    
    // Find min of change in u and v across quad: compute du and dv magnitude across quad
    float2 dTexCoords = dxSize * dxSize + dySize * dySize;
    
    // Standard mipmapping uses max here
    float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);
    
    // Compute the current mip level
    float mipLevel = max(0.5 * log2(minTexCoordDelta), 0.0);
    
    // Offset mip level to sample as if texture were 512x512
    float mipOffset = log2(actualTextureDims.x / 512.0);
 
    return mipLevel + mipOffset;
}

	float GetHeight(PS_INPUT input, float2 currentOffset, float3 normalsAmplitude, float3 normalScalesRcp, float3 mipLevels)
	{
		float3 heights;
		heights.x = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevels.x).w;
		heights.y = Normals02Tex.SampleLevel(Normals02Sampler, input.TexCoord1.zw + currentOffset * normalScalesRcp.y, mipLevels.y).w;
		heights.z = Normals03Tex.SampleLevel(Normals03Sampler, input.TexCoord2.xy + currentOffset * normalScalesRcp.z, mipLevels.z).w;
		heights = 1.0 - heights;
		heights *= normalsAmplitude;
		return heights.x + heights.y + heights.z;
	}

	float2 GetParallaxOffset(PS_INPUT input, float3 normalsAmplitude, float3 normalScalesRcp)
	{
		float3 viewDirection = normalize(input.WPosition.xyz);
		float2 parallaxOffsetTS = viewDirection.xy / -viewDirection.z;

		// Parallax scale is also multiplied by normalScalesRcp
		parallaxOffsetTS *= 20.0;

		float3 mipLevels;
		mipLevels.x = GetMipLevel(input.TexCoord1.xy, Normals01Tex);
		mipLevels.y = GetMipLevel(input.TexCoord1.zw, Normals02Tex);
		mipLevels.z = GetMipLevel(input.TexCoord2.xy, Normals03Tex);

		float stepSize = rcp(16.0);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] while (currHeight > currBound)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetHeight(input, currBound * parallaxOffsetTS.xy, normalsAmplitude, normalScalesRcp, mipLevels);
		}

		float prevBound = currBound - stepSize;

		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		float denominator = delta2 - delta1;
		float parallaxAmount = (currBound * delta2 - prevBound * delta1) / denominator;

		return parallaxOffsetTS.xy * parallaxAmount;
	}

#if defined(FLOWMAP)
	float GetFlowmapHeight(PS_INPUT input, float2 uvShift, float multiplier, float offset, float mipLevel)
	{
		FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
		float2 baseUV = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));
		return FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, baseUV, mipLevel).w;
	}

	float GetFlowmapBlendedHeight(PS_INPUT input, float2 normalMul, float2 uvShift, float mipLevel)
	{
		float height0 = GetFlowmapHeight(input, uvShift, 9.92, 0, mipLevel);
		float height1 = GetFlowmapHeight(input, float2(0, uvShift.y), 10.64, 0.27, mipLevel);
		float height2 = GetFlowmapHeight(input, 0.0.xx, 8, 0, mipLevel);
		float height3 = GetFlowmapHeight(input, float2(uvShift.x, 0), 8.48, 0.62, mipLevel);

		float blendedHeight =
			normalMul.y * (normalMul.x * height2 + (1 - normalMul.x) * height3) +
			(1 - normalMul.y) * (normalMul.x * height1 + (1 - normalMul.x) * height0);

		return blendedHeight;
	}

	float GetFlowmapParallaxAmount(PS_INPUT input, float2 flowmapDims, float3 viewDirection)
	{
		float viewDotUp = -viewDirection.z;

		if (viewDotUp < 0.05)
			return 0.0;

		float2 parallaxDir = viewDirection.xy / -viewDirection.z;
		parallaxDir.y = -parallaxDir.y;

		float parallaxScale = 0.008 * saturate(viewDotUp * 2.0);
		parallaxDir *= parallaxScale;

		float2 uvShiftPx = 1 / (128 * flowmapDims);

		int numSteps = (int)lerp(32.0, 8.0, viewDotUp);
		float stepSize = rcp((float)numSteps);

		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] for (int i = 0; i < numSteps && currHeight > currBound; i++)
		{
			prevHeight = currHeight;
			currBound += stepSize;

			PS_INPUT offsetInput = input;
			offsetInput.TexCoord3.xy = input.TexCoord3.xy + currBound * parallaxDir;

			float2 cellBlend = 0.5 + -(-0.5 + abs(frac(offsetInput.TexCoord2.zw * (64 * flowmapDims)) * 2 - 1));
			currHeight = 1.0 - GetFlowmapBlendedHeight(offsetInput, cellBlend, uvShiftPx, 0);
		}

		float prevBound = currBound - stepSize;
		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		float denominator = delta2 - delta1;

		return denominator != 0.0 ? (currBound * delta2 - prevBound * delta1) / denominator : currBound;
	}

	float GetFlowmapParallaxHeight(PS_INPUT input, float2 currentOffset, float3 normalScalesRcp, float mipLevel)
	{
		float height = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevel).w;
		height *= NormalsAmplitude.x;
		return 1.0 - height;
	}

	float2 GetFlowmapParallaxUVOffset(PS_INPUT input, float3 viewDirection, float3 normalScalesRcp)
	{
		float2 parallaxOffsetTS = viewDirection.xy / -viewDirection.z;
		parallaxOffsetTS *= 80.0;

		float2 textureDims;
		Normals01Tex.GetDimensions(textureDims.x, textureDims.y);
#if defined(VR)
		textureDims /= 16.0;
#else
		textureDims /= 8.0;
#endif
		float2 texCoordsPerSize = input.TexCoord1.xy * textureDims;
		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);
		float2 dTexCoords = dxSize * dxSize + dySize * dySize;
		float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);
#if defined(VR)
		mipLevel += 4;
#else
		mipLevel += 3;
#endif

		float stepSize = rcp(16.0);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] while (currHeight > currBound)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetFlowmapParallaxHeight(input, currBound * parallaxOffsetTS.xy, normalScalesRcp, mipLevel);
		}

		float prevBound = currBound - stepSize;
		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		float denominator = delta2 - delta1;
		float parallaxAmount = (currBound * delta2 - prevBound * delta1) / denominator;

		return parallaxOffsetTS.xy * parallaxAmount;
	}

	float2 GetFlowmapParallaxOffset(PS_INPUT input, float2 flowmapDimensions, float3 viewDirection, float3 normalScalesRcp)
	{
		return GetFlowmapParallaxUVOffset(input, viewDirection, normalScalesRcp);
	}
#endif
}
