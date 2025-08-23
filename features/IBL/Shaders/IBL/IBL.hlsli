#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace ImageBasedLighting
{
#if defined(IBL_AMBIENTCOMPOSITE)
	Texture2D<sh2> DiffuseIBLTexture : register(t8);
#elif defined(IBL_DEFERRED)
	Texture2D<sh2> DiffuseIBLTexture : register(t14);
#else
	Texture2D<sh2> DiffuseIBLTexture : register(t76);
#endif
	float3 GetDiffuseIBL(float3 rayDir)
	{
		sh2 shR = DiffuseIBLTexture.Load(int3(0, 0, 0));
		sh2 shG = DiffuseIBLTexture.Load(int3(1, 0, 0));
		sh2 shB = DiffuseIBLTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return float3(colorR, colorG, colorB) / Math::PI;
	}

	float3 GetFogIBLColor(float3 fogColor)
	{
		float3 directionalAmbientColor = max(0, mul(SharedData::DirectionalAmbient, float4(float3(0, 0, 0), 1.0))).xyz;
		float3 iblColor = directionalAmbientColor * SharedData::iblSettings.DALCAmount + Color::Saturation(GetDiffuseIBL(float3(0, 0, 0)), SharedData::iblSettings.IBLSaturation) * SharedData::iblSettings.DiffuseIBLScale;
		if (SharedData::iblSettings.PreserveFogLuminance) {
			const float fogLuminance = Color::RGBToLuminance(fogColor);
			const float iblLuminance = Color::RGBToLuminance(iblColor);
			if (iblLuminance > 0) {
				const float scale = fogLuminance / iblLuminance;
				iblColor *= scale;
			} else {
				iblColor = fogColor;
			}
		}
		return lerp(fogColor, iblColor, SharedData::iblSettings.FogAmount);
	}
}