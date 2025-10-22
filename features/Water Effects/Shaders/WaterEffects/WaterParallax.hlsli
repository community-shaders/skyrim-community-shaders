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
}
