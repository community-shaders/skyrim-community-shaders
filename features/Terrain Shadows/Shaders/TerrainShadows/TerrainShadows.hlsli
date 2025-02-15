namespace TerrainShadows
{
	Texture2D<float2> ShadowHeightTexture : register(t60);

	float2 GetTerrainShadowUV(float2 xy)
	{
		return xy * SharedData::terraOccSettings.Scale.xy + SharedData::terraOccSettings.Offset.xy;
	}

	float GetTerrainZ(float norm_z)
	{
		return lerp(SharedData::terraOccSettings.ZRange.x, SharedData::terraOccSettings.ZRange.y, norm_z) - 1024;
	}

	float2 GetTerrainZ(float2 norm_z)
	{
		return float2(GetTerrainZ(norm_z.x), GetTerrainZ(norm_z.y));
	}

	float GetTerrainShadow(const float3 worldPos, SamplerState samp)
	{
		if (SharedData::terraOccSettings.EnableTerrainShadow) {
			float2 terraOccUV = GetTerrainShadowUV(worldPos.xy);
			float2 shadowHeight = GetTerrainZ(ShadowHeightTexture.SampleLevel(samp, terraOccUV, 0));
			float shadowFraction = saturate((worldPos.z - shadowHeight.y) / (shadowHeight.x - shadowHeight.y));
			return shadowFraction;
		}

		return 1.0;
	}
}
