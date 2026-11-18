#ifndef TERRAIN_SHADOW_REGISTER
#	define TERRAIN_SHADOW_REGISTER t60
#endif

namespace TerrainShadows
{
	Texture2D<float2> ShadowHeightTexture : register(TERRAIN_SHADOW_REGISTER);

	// Lowers shadow heights to hide self-shadowing from the coarse heightmap.
	static const float SelfShadowBias = 256.0;

	float2 GetTerrainShadowUV(float2 xy)
	{
		return xy * SharedData::terraOccSettings.Scale.xy + SharedData::terraOccSettings.Offset.xy;
	}

	float GetTerrainZ(float norm_z)
	{
		return lerp(SharedData::terraOccSettings.ZRange.x, SharedData::terraOccSettings.ZRange.y, norm_z) - SelfShadowBias;
	}

	float2 GetTerrainZ(float2 norm_z)
	{
		return float2(GetTerrainZ(norm_z.x), GetTerrainZ(norm_z.y));
	}

	float GetTerrainShadow(const float3 worldPos, SamplerState samp)
	{
		if (!SharedData::terraOccSettings.EnableTerrainShadow)
			return 1.0;
		float2 uv = GetTerrainShadowUV(worldPos.xy);
		if (any(uv < 0.0) || any(uv > 1.0))
			return 1.0;
		float2 shadowHeight = GetTerrainZ(ShadowHeightTexture.SampleLevel(samp, uv, 0));
		// Blurring in z hides the heightmap's xy resolution; capped by the bias so lit flat terrain stays lit.
		float zBlur = min(SharedData::terraOccSettings.ZBlur, SelfShadowBias);
		float lowerHeight = shadowHeight.y - zBlur;
		float transitionHeight = shadowHeight.x + zBlur - lowerHeight;
		if (transitionHeight <= 0.0)
			return worldPos.z >= shadowHeight.x ? 1.0 : 0.0;
		return saturate((worldPos.z - lowerHeight) / transitionHeight);
	}
}
