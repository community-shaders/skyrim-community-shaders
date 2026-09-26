#ifndef __COLOR_MANAGEMENT_DEPENDENCY_HLSL__
#define __COLOR_MANAGEMENT_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"
#if defined(PSHADER)
#	include "Common/Permutation.hlsli"
#endif

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
namespace Color
{
#	if defined(TRUE_PBR)
	static const float AlbedoScale = 1.0;
#	else
	static const float AlbedoScale = SharedData::linearLightingSettings.vanillaDiffuseColorMult;
#	endif

#	if defined(ENABLE_LL)
	static const float BRDFScale = Math::INV_PI;
#	else
	static const float BRDFScale = 1.0;
#	endif

	float3 Albedo(float3 color)
	{
#	if defined(EFFECTS11)
		if (SharedData::enbSettings.Enable)
			color = pow(abs(color), SharedData::enbSettings.ColorPow);
#	endif
		return color * AlbedoScale;
	}

	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR)
		return color * PBRLightingCompensation;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}

	float3 DirectionalLight(float3 color)
	{
		return Light(color) * SharedData::linearLightingSettings.directionalLightMult;
	}

	float3 PointLight(float3 color)
	{
		return Light(color) * SharedData::linearLightingSettings.pointLightMult;
	}

	float3 Ambient(float3 color)
	{
		return color * SharedData::linearLightingSettings.ambientMult;
	}

	float3 EffectMult(float3 color)
	{
#	if defined(MEMBRANE)
		color *= SharedData::linearLightingSettings.membraneEffectMult;
#	elif defined(BLOOD)
		color *= SharedData::linearLightingSettings.bloodEffectMult;
#	elif defined(PROJECTED_UV)
		color *= SharedData::linearLightingSettings.projectedEffectMult;
#	elif defined(DEFERRED)
		color *= SharedData::linearLightingSettings.deferredEffectMult;
#	else
		color *= SharedData::linearLightingSettings.otherEffectMult;
#	endif
		return color;
	}

	static const float EffectLightingScale = SharedData::linearLightingSettings.effectLightingMult;
}
#endif

namespace ColorManagement
{
	static const float LEGACY_TEXTURE_GAMMA = 1.8;

	float3 TextureToWorking(float3 color, bool linearInput = false)
	{
#if defined(ENABLE_LL)
		if (!linearInput)
			color = pow(abs(color), LEGACY_TEXTURE_GAMMA);
		return Color::LinearSRGBToWorking(color);
#else
		return color;
#endif
	}

	float4 TextureToWorking(float4 color, bool linearInput = false)
	{
		return float4(TextureToWorking(color.rgb, linearInput), color.a);
	}

	float3 SRGBToWorking(float3 color)
	{
#if defined(ENABLE_LL)
		return Color::LinearSRGBToWorking(TransferFunctions::SRGBToLinear(color));
#else
		return color;
#endif
	}

	float4 SRGBToWorking(float4 color)
	{
		return float4(SRGBToWorking(color.rgb), color.a);
	}

	float SRGBToWorking(float color)
	{
#if defined(ENABLE_LL)
		return TransferFunctions::SRGBToLinear(color);
#else
		return color;
#endif
	}

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	float3 PBRVertexColorToLinear(float3 encodedVertexColor)
	{
#	if defined(EFFECTS11)
		if (SharedData::enbSettings.Enable)
			return TransferFunctions::Gamma22ToLinear(encodedVertexColor);
#	endif
#	if defined(ENABLE_LL)
		return TransferFunctions::SRGBToLinear(encodedVertexColor);
#	else
		return encodedVertexColor;
#	endif
	}

	float3 WorkingToUI(float3 color)
	{
#	if defined(ENABLE_LL)
#		if defined(ENABLE_ACESCG)
		color = AP1TosRGB(color);
#		endif
		return TransferFunctions::LinearToGamma22(color);
#	else
		return color;
#	endif
	}

	float SceneToLinear(float sceneValue)
	{
#	if defined(ENABLE_LL)
		return sceneValue;
#	else
		return TransferFunctions::GameGammaToLinear(sceneValue);
#	endif
	}

	float3 SceneToLinear(float3 sceneValue)
	{
#	if defined(ENABLE_LL)
		return sceneValue;
#	else
		return TransferFunctions::GameGammaToLinear(sceneValue);
#	endif
	}

	float3 LinearToScene(float3 linearValue)
	{
#	if defined(ENABLE_LL)
		return linearValue;
#	else
		return TransferFunctions::LinearToGameGamma(linearValue);
#	endif
	}

	namespace SceneColor
	{
		float ScaleByLinear(float sceneColor, float linearScale)
		{
#	if defined(ENABLE_LL)
			return sceneColor * linearScale;
#	else
			return abs(sceneColor) * TransferFunctions::LinearToGameGamma(linearScale);
#	endif
		}

		float3 ScaleByLinear(float3 sceneColor, float linearScale)
		{
#	if defined(ENABLE_LL)
			return sceneColor * linearScale;
#	else
			return abs(sceneColor) * TransferFunctions::LinearToGameGamma(linearScale);
#	endif
		}

		float3 ScaleByLinear(float3 sceneColor, float3 linearScale)
		{
#	if defined(ENABLE_LL)
			return sceneColor * linearScale;
#	else
			return abs(sceneColor) * TransferFunctions::LinearToGameGamma(linearScale);
#	endif
		}

		float3 ScaleAndAddLinear(float3 sceneColor, float3 linearScale, float3 linearOffset)
		{
			return LinearToScene(SceneToLinear(sceneColor) * linearScale + linearOffset);
		}

		float3 LerpInLinear(float3 lhsSceneColor, float3 rhsSceneColor, float weight)
		{
			return LinearToScene(lerp(SceneToLinear(lhsSceneColor), SceneToLinear(rhsSceneColor), weight));
		}
	}

#endif
}

#endif
