
namespace ScreenSpaceShadows
{
	Texture2D<unorm float2> ScreenSpaceShadowsTexture : register(t45);

	float2 GetScreenSpaceShadows(float3 screenPosition, float2 uv, float noise)
	{
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition.xy + 0.5f), 0)).xy;
	}

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise)
	{
		return GetScreenSpaceShadows(screenPosition, uv, noise).x;
	}
}
