#ifndef __COLOR_MANAGEMENT_DEPENDENCY_HLSL__
#define __COLOR_MANAGEMENT_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"
#if defined(PSHADER)
#	include "Common/Permutation.hlsli"
#endif

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
namespace Color
{
	// Legacy lighting lobes are calibrated against Lambert diffuse.
#	if defined(ENABLE_LL)
	static const float BRDFScale = Math::INV_PI;
#	else
	static const float BRDFScale = 1.0;
#	endif

	/** @brief Gamma lighting pipelines (Linear Lighting off, or an Effects 11 preset) keep PBR material colors gamma 2.2 encoded. */
#	if !defined(ENABLE_LL)
	static const bool PBRMaterialIsGammaEncoded = true;
#	elif defined(EFFECTS11)
	static const bool PBRMaterialIsGammaEncoded = SharedData::enbSettings.Enable;
#	else
	static const bool PBRMaterialIsGammaEncoded = false;
#	endif

	/** @brief Decodes a PBR material color for linear math. */
	float3 PBRMaterialToLinear(float3 materialColor)
	{
		return PBRMaterialIsGammaEncoded ? TransferFunctions::Gamma22ToLinear(materialColor) : materialColor;
	}

	/** @brief Inverse of PBRMaterialToLinear. */
	float3 LinearToPBRMaterial(float3 linearColor)
	{
		return PBRMaterialIsGammaEncoded ? TransferFunctions::LinearToGamma22(linearColor) : linearColor;
	}

	float3 Albedo(float3 color)
	{
#	if defined(EFFECTS11)
		if (SharedData::enbSettings.Enable)
			color = pow(abs(color), SharedData::enbSettings.ColorPow);
#	endif
#	if defined(TRUE_PBR)
		color = LinearToPBRMaterial(color);
#	endif
		return color;
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

	/** @brief Per-type effect scale. Additive effects (fire and light sprites) skip the "other" multiplier for their own. */
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
#	elif !defined(ADDBLEND)
		color *= SharedData::linearLightingSettings.otherEffectMult;
#	endif
		return color;
	}

	/** @brief Property color scale for non-fire, non-sky effects: Effects 11 PARTICLE Intensity when its preset is on, else Linear Lighting. */
	float ParticleEffectMult()
	{
#	if defined(EFFECTS11)
		return SharedData::enbSettings.Enable ? SharedData::enbSettings.ParticleIntensity : 1.0;
#	else
		return SharedData::linearLightingSettings.particleEffectMult;
#	endif
	}

	/** @brief Scales additive effects: fire gets a curve and intensity, all other additive effects are light sprites. */
	float3 AdditiveEffect(float3 color, bool isFire)
	{
#	if defined(EFFECTS11)
		if (!SharedData::enbSettings.Enable)
			return color;
		const float fireCurve = SharedData::enbSettings.FireCurve;
		const float fireIntensity = SharedData::enbSettings.FireIntensity;
		const float lightSpriteIntensity = SharedData::enbSettings.LightSpriteIntensity;
#	elif defined(ENABLE_LL)
		const float fireCurve = SharedData::linearLightingSettings.fireEffectCurve;
		const float fireIntensity = SharedData::linearLightingSettings.fireEffectMult;
		const float lightSpriteIntensity = SharedData::linearLightingSettings.lightSpriteEffectMult;
#	endif
#	if defined(EFFECTS11) || defined(ENABLE_LL)
		color = isFire ? pow(abs(color), fireCurve) * fireIntensity : color * lightSpriteIntensity;
#	endif
		return color;
	}

	static const float EffectLightingScale = SharedData::linearLightingSettings.effectLightingMult;
}
#endif

namespace ColorManagement
{
	static const float LEGACY_TEXTURE_GAMMA = 1.8;

	float3 DiffuseToWorking(float3 color, bool linearInput = false)
	{
#if defined(ENABLE_LL)
		if (!linearInput) {
			color = pow(saturate(color), SharedData::linearLightingSettings.diffuseGamma);
			float3 scaledColor = SharedData::linearLightingSettings.diffuseCurve * color;
			color = SharedData::linearLightingSettings.diffuseWhiteReflectance * (scaledColor / (1.0 - color + scaledColor));
		}
		return Color::LinearSRGBToWorking(color);
#else
		return color;
#endif
	}

	float4 DiffuseToWorking(float4 color, bool linearInput = false)
	{
		return float4(DiffuseToWorking(color.rgb, linearInput), color.a);
	}

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
		return Color::PBRMaterialIsGammaEncoded ? TransferFunctions::Gamma22ToLinear(encodedVertexColor) : TransferFunctions::SRGBToLinear(encodedVertexColor);
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
