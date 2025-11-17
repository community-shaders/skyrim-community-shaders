#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace Weather
{
	Texture2D<sh2> DiffuseIBLTexture : register(t100);

	float3 GetDiffuseIBL(float3 rayDir)
	{
		sh2 shR = DiffuseIBLTexture.Load(int3(0, 0, 0));
		sh2 shG = DiffuseIBLTexture.Load(int3(1, 0, 0));
		sh2 shB = DiffuseIBLTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return float3(colorR, colorG, colorB);
	}
}
