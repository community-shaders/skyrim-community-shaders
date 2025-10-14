#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace ImageBasedLighting
{
#if defined(IBL_DEFERRED)
	Texture2D<sh2> DiffuseIBLTexture : register(t14);
	Texture2D<sh2> DiffuseSkyIBLTexture : register(t15);
#else
	Texture2D<sh2> DiffuseIBLTexture : register(t76);
	Texture2D<sh2> DiffuseSkyIBLTexture : register(t77);
	TextureCube<float4> StaticDiffuseIBLTexture : register(t78);
	TextureCube<float4> StaticSpecularIBLTexture : register(t79);
#endif
	float3 GetDiffuseIBL(float3 rayDir)
	{
		sh2 shR = DiffuseSkyIBLTexture.Load(int3(0, 0, 0));
		sh2 shG = DiffuseSkyIBLTexture.Load(int3(1, 0, 0));
		sh2 shB = DiffuseSkyIBLTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return float3(colorR, colorG, colorB) / Math::PI;
	}

	float3 GetIBLColor(float3 rayDir)
	{
		float3 color = 0;
		if (!SharedData::InInterior){
			color = GetDiffuseIBL(rayDir);
		}
		return color;
	}

#if defined(LIGHTING)
	float3 GetStaticDiffuseIBL(float3 N, SamplerState samp)
	{
		return StaticDiffuseIBLTexture.SampleLevel(samp, N.xzy, 0).xyz / Math::PI;
	}
#endif
}

#endif // __IBL_HLSLI__