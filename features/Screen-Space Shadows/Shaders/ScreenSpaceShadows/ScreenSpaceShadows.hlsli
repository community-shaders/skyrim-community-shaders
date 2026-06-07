
namespace ScreenSpaceShadows
{
	Texture2D<unorm float> ScreenSpaceShadowsTexture : register(t45);

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise)
	{
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition.xy + 0.5f), 0)).x;
	}
}