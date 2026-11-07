
namespace ScreenSpaceShadows
{
	Texture2D<unorm float2> ScreenSpaceShadowsTexture : register(t45);

	float2 GetScreenSpaceShadows(float3 screenPosition, float2 uv, float noise)
	{
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition.xy), 0)).xy;
	}

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise)
	{
		return GetScreenSpaceShadows(screenPosition, uv, noise).x;
	}

	// Depth only represents the visible face. Back-side rays would shadow the receiver itself.
	float GetThinSurfaceScreenSpaceShadow(float3 screenPosition, float2 uv, float noise, float faceNdotL)
	{
		float weight = saturate(4.0 * faceNdotL);
		float shadow = 1.0;
		[branch] if (weight > 0.0)
			shadow = lerp(1.0, GetScreenSpaceShadow(screenPosition, uv, noise), weight);
		return shadow;
	}
}
