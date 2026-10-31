// SPDX-License-Identifier: MIT AND (LicenseRef-GPL-3.0-with-Modding-Exception WITH GPL-3.0-linking-source-exception)
// Original code Copyright (c) 2022 Ilya Perapechka (MIT), modifications GPL-3.0 with Modding Exception (see EXCEPTIONS.md)

#include "Common/ColorManagement.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"

#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

#if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_REFRACTION)
namespace Raindrop
{
	static const float Radius = 0.003 / GAME_UNIT_TO_M;
	static const float MinimumRadiusPixels = 1.0;  // Smaller drops widen to this and fade by the area they gained
}
#endif

#if defined(VSHADER)
struct VS_INPUT
{
	float4 Position: POSITION0;
#	if !defined(ENVCUBE)
	float4 Normal: NORMAL0;
#	endif
	float4 TexCoord0: TEXCOORD0;
#	if defined(ENVCUBE)
	float4
#	else
	int4
#	endif
		TexCoord1: TEXCOORD1;
};
#endif

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
	float4 Color: COLOR0;
	float2 TexCoord0: TEXCOORD0;
#if defined(ENVCUBE)
	float4 PrecipitationOcclusionTexCoord: TEXCOORD1;
#endif
#if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_REFRACTION)
	float3 RaindropData: TEXCOORD2;  // xy: raindrop texture UV, z: coverage left after widening to the minimum size
#endif
};

#ifdef VSHADER
cbuffer PerTechnique : register(b0)
{
	float2 ScaleAdjust : packoffset(c0);
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj;  // 0
	row_major float4x4 WorldView;      // 4
#	if defined(ENVCUBE)
	row_major float4x4 PrecipitationOcclusionWorldViewProj;  // 8, 16
#	endif
	float4 fVars0;        // 8, 16 ENVCUBE 12, 20
	float4 fVars1;        // 9, 17 ENVCUBE 13, 21
	float4 fVars2;        // 10, 18 ENVCUBE 14, 22
	float4 fVars3;        // 11, 19 ENVCUBE 15, 23
	float4 fVars4;        // 12, 20 ENVCUBE 16, 24
	float4 Color1;        // 13, 21 ENVCUBE 17, 25
	float4 Color2;        // 14, 22 ENVCUBE 18, 26
	float4 Color3;        // 15, 23 ENVCUBE 19, 27
	float4 Velocity;      // 16, 24 ENVCUBE 20, 28
	float4 Acceleration;  // 17, 25 ENVCUBE 21, 29
	float4 Wind;          // 18, 26 ENVCUBE 22, 30
}

float2x2 GetRotationMatrix(float angle)
{
	float sine, cosine;
	sincos(angle, sine, cosine);

	return float2x2(float2(cosine, -sine), float2(sine, cosine));
}

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

#	if defined(ENVCUBE)
#		if defined(RAIN)
	float2 positionOffset = input.TexCoord1.xy;
#		else
	float2x2 rotationMatrix = GetRotationMatrix(fVars0.w);
	float2 positionOffset = mul(rotationMatrix, input.TexCoord1.xy) * ScaleAdjust + mul(rotationMatrix, input.TexCoord1.zw);
#		endif

	float3 normalizedPosition = (fVars0.xyz + input.Position.xyz) / fVars2.xxx;
	normalizedPosition = normalizedPosition >= -normalizedPosition ? frac(abs(normalizedPosition)) :
	                                                                 -frac(abs(normalizedPosition));

	float4 msPosition;
	msPosition.xyz = normalizedPosition * fVars2.xxx + (-(fVars2.x * 0.5).xxx + fVars1.xyz);
	msPosition.w = 1;

#		if defined(RAIN) && defined(RAIN_REFRACTION)
	vsout.RaindropData.z = 1.0;
	if (SharedData::rainRefractionSettings.Enable) {
		// Physically sized drop, centered on the vanilla streak and stretched along its motion
		float2 corner = sign(input.TexCoord1.xy);
		float3 motion = Velocity.xyz * abs(input.TexCoord1.y) * SharedData::rainRefractionSettings.MotionStretch;
		float4 center = msPosition - float4(Velocity.xyz * 0.5, 0);
		float4 leading = mul(WorldViewProj, center + float4(motion * 0.5, 0));
		float4 trailing = mul(WorldViewProj, center - float4(motion * 0.5, 0));
		float4 dropPosition = lerp(trailing, leading, corner.y * 0.5 + 0.5);

		float2 renderSize = SharedData::BufferDim.xy * FrameBuffer::DynamicResolutionParams1.xy;
		float2 radiusClip = Raindrop::Radius * SharedData::rainRefractionSettings.TextureRadiusScale * abs(float2(FrameBuffer::CameraProj[0][0], FrameBuffer::CameraProj[1][1]));
		float2 radiusPixels = radiusClip * renderSize * 0.5 / max(abs(dropPosition.w), EPSILON_DIVISION);
		float rasterScale = max(1.0, Raindrop::MinimumRadiusPixels / max(min(radiusPixels.x, radiusPixels.y), EPSILON_DIVISION));
		float2 motionPixels = (leading.xy / max(abs(leading.w), EPSILON_DIVISION) - trailing.xy / max(abs(trailing.w), EPSILON_DIVISION)) * renderSize * 0.5;

		// Fade by the ratio of the true drop's area to the widened one's, so sub-pixel drops keep their brightness
		float radius = sqrt(radiusPixels.x * radiusPixels.y);
		float rasterRadius = radius * rasterScale;
		float motionLength = length(motionPixels);
		float dropArea = Math::PI * radius * radius + 2.0 * radius * motionLength;
		float rasterArea = Math::PI * rasterRadius * rasterRadius + 2.0 * rasterRadius * motionLength;
		vsout.RaindropData.z = saturate(dropArea / max(rasterArea, EPSILON_DIVISION));

		vsout.Position = dropPosition;
		vsout.Position.xy += corner * radiusClip * rasterScale;
	} else
#		endif
	{
		float4 viewPosition = mul(WorldViewProj, msPosition);
#		if defined(RAIN)
		float4 adjustedMsPosition = msPosition - float4(Velocity.xyz, 0);
		float positionBlendParam = 0.5 * (1 + input.TexCoord1.y);
		float4 adjustedViewPosition = mul(WorldViewProj, adjustedMsPosition);
		float4 finalViewPosition = lerp(adjustedViewPosition, viewPosition, positionBlendParam);
#		else
		float4 finalViewPosition = viewPosition;
#		endif
		vsout.Position.xy = positionOffset + finalViewPosition.xy;
		vsout.Position.zw = finalViewPosition.zw;
	}
	vsout.Color.xyz = 1.0.xxx;
	vsout.Color.w = fVars1.w;

	vsout.TexCoord0.xy = input.TexCoord0.xy;

	float4 precipitationOcclusionTexCoord = mul(PrecipitationOcclusionWorldViewProj, msPosition);
	precipitationOcclusionTexCoord.y = -precipitationOcclusionTexCoord.y;
	vsout.PrecipitationOcclusionTexCoord = precipitationOcclusionTexCoord;
#	else
	float tmp2 = input.Normal.w * input.Position.w;
	float tmp1 = tmp2 / fVars0.y;

	float uvScale1, uvScale2, tmp3, tmp4;
	if (tmp1 > fVars2.w) {
		uvScale1 = fVars2.y;
		uvScale2 = 0;
		tmp3 = fVars2.w;
		tmp4 = 1;
	} else if (tmp1 > fVars2.z) {
		uvScale1 = fVars2.x;
		uvScale2 = fVars2.y;
		tmp3 = fVars2.z;
		tmp4 = fVars2.w;
	} else {
		uvScale1 = 0;
		uvScale2 = fVars2.x;
		tmp3 = 0;
		tmp4 = fVars2.z;
	}
	float uvScaleParam = (tmp1 - tmp3) / (tmp4 - tmp3);
	float uvScale = lerp(uvScale1, uvScale2, uvScaleParam);

	vsout.TexCoord0.xy = fVars4.xy * input.TexCoord1.xy;

	float2 uv1 = (input.TexCoord1.zw * 2.0.xx - 1.0.xx) * uvScale;
	float uvAngle = input.TexCoord0.y * input.Position.w + input.TexCoord0.x;
	float2x2 rotationMatrix = GetRotationMatrix(uvAngle);
	float2 positionOffset = mul(rotationMatrix, uv1);

	float4 msPosition;
	msPosition.xyz = -fVars3.xyz +
	                 (((input.Normal.xyz * fVars0.www + Acceleration.xyz) * (tmp2 * tmp2)) * 0.5 +
						 (((fVars0.zzz * input.Normal.xyz) * input.TexCoord0.zzz +
							  (normalize(-Wind.xyz + input.Position.xyz) * Wind.www + Velocity.xyz)) *
								 tmp2 +
							 input.Position.xyz));
	msPosition.w = 1;

	float4 viewPosition = mul(WorldViewProj, msPosition);
	vsout.Position.xy = positionOffset * ScaleAdjust + viewPosition.xy;
	vsout.Position.zw = viewPosition.zw;

	float4 color1, color2;
	float colorTmp1, colorTmp2;
	if (tmp1 > fVars1.z) {
		color1 = Color3.xyzw;
		color2 = float4(Color3.xyz, 0);
		colorTmp1 = fVars1.z;
		colorTmp2 = 1;
	} else if (tmp1 > fVars1.y) {
		color1 = Color2.xyzw;
		color2 = Color3.xyzw;
		colorTmp1 = fVars1.y;
		colorTmp2 = fVars1.z;
	} else if (tmp1 > fVars1.x) {
		color1 = Color1.xyzw;
		color2 = Color2.xyzw;
		colorTmp1 = fVars1.x;
		colorTmp2 = fVars1.y;
	} else {
		color1 = float4(Color1.xyz, 0);
		color2 = Color1.xyzw;
		colorTmp1 = 0;
		colorTmp2 = fVars1.x;
	}
#		if !defined(GRAYSCALE_TO_COLOR)
	color1.rgb = ColorManagement::SRGBToWorking(color1.rgb);
	color2.rgb = ColorManagement::SRGBToWorking(color2.rgb);
#		endif
	float colorParam = (tmp1 - colorTmp1) / (colorTmp2 - colorTmp1);
	float4 color = lerp(color1, color2, colorParam);

	vsout.Color.w = fVars3.w * color.w;
	vsout.Color.xyz = color.xyz;
#	endif

#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_REFRACTION)
	// Corner offsets are +-particle size; their sign picks the corner of the drop's covered area in the raindrop texture, so one drop fills each particle
	vsout.RaindropData.xy = lerp(SharedData::rainRefractionSettings.TextureBounds.xy, SharedData::rainRefractionSettings.TextureBounds.zw, step(0, input.TexCoord1.xy));
#	endif

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
	float4 Normal: SV_Target1;
};

#ifdef PSHADER

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

SamplerState SampSourceTexture : register(s0);
#	if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
SamplerState SampGrayscaleTexture : register(s1);
#	endif
#	if defined(ENVCUBE)
SamplerState SampPrecipitationOcclusionTexture : register(s2);
SamplerState SampUnderwaterMask : register(s3);
#	endif

Texture2D<float4> TexSourceTexture : register(t0);
#	if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
Texture2D<float4> TexGrayscaleTexture : register(t1);
#	endif
#	if defined(ENVCUBE)
Texture2D<float4> TexPrecipitationOcclusionTexture : register(t2);
Texture2D<float4> TexUnderwaterMask : register(t3);
#	endif
#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_REFRACTION)
Texture2D<float4> TexRaindropNormals : register(t80);
#	endif

cbuffer PerGeometry : register(b2)
{
	float ColorScale : packoffset(c0);
	float3 TextureSize : packoffset(c1);
};

#	define LinearSampler SampSourceTexture
#	include "Common/Permutation.hlsli"
#	include "Common/ShadowSampling.hlsli"

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(DYNAMIC_CUBEMAPS)
#		define SampColorSampler SampSourceTexture
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(ENVCUBE) && defined(RAIN) && defined(DYNAMIC_CUBEMAPS) && defined(RAIN_REFRACTION)
#		define REFRACTIVE_RAIN
#		include "Common/BRDF.hlsli"

namespace Raindrop
{
	static const float WaterF0 = 0.02;
	static const float Roughness = 0.3;              // Perceptual roughness of the drop's surface highlight
	static const float TransmissionExponent = 32.0;  // Tightness of the glow of a light seen through a drop
}
#	endif

/** @brief What a light shades: an unlit particle that takes light as-is, or a raindrop surface. */
struct ParticleSurface
{
	bool isRaindrop;
	float3 normal;
	float3 view;
	float3 refractDir;  ///< View ray continued through the drop
};

/** @brief Light a particle sends toward the viewer from lightColor arriving along L (toward the light). */
float3 ShadeLight(ParticleSurface surface, float3 lightColor, float3 L)
{
#	if defined(REFRACTIVE_RAIN)
	if (surface.isRaindrop) {
		float3 H = normalize(surface.view + L);
		float NdotL = saturate(dot(surface.normal, L));
		float NdotV = saturate(dot(surface.normal, surface.view));
		float NdotH = saturate(dot(surface.normal, H));
		float VdotH = saturate(dot(surface.view, H));
		float3 specular = BRDF::D_GGX(Raindrop::Roughness, NdotH) * BRDF::Vis_SmithJointApprox(Raindrop::Roughness, NdotV, NdotL) *
		                  BRDF::F_Schlick(Raindrop::WaterF0, VdotH) * NdotL;

		// A light behind the drop is seen through it along the refracted view ray; normalized Phong lobe
		float transmissionLobe = pow(saturate(dot(surface.refractDir, L)), Raindrop::TransmissionExponent) * (Raindrop::TransmissionExponent + 2.0) / Math::TAU;
		float3 transmission = transmissionLobe * (1.0 - BRDF::F_Schlick(Raindrop::WaterF0, NdotV));

		return ColorManagement::SceneToLinear(lightColor) * (specular + transmission);
	}
#	endif
	return lightColor;
}

/** @brief Sun color at positionWS after directional, world and cloud shadows. */
float3 GetDirLightColor(float3 positionWS)
{
	float unusedDetailedShadow;
	float3 dirLightColor = SharedData::DirLightColor.xyz * ShadowSampling::GetLightingShadow(positionWS, unusedDetailedShadow);
	// Menu and reflection passes don't match the world heightmap and cloud state
	if (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld)
		dirLightColor *= ShadowSampling::GetWorldShadow(positionWS, FrameBuffer::CameraPosAdjust.xyz);
	return dirLightColor;
}

/** @brief Adds the clustered point lights at positionWS, each shaded by ShadeLight, to lighting in light order. */
void AddPointLighting(float3 positionWS, ParticleSurface surface, inout float3 lighting)
{
#	if defined(LIGHT_LIMIT_FIX)
	float3 viewPosition = FrameBuffer::WorldToView(positionWS);
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);

	uint clusterIndex = 0;
	if (LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		uint lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
			LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];
			const bool localShadow = light.lightFlags & LightLimitFix::LightFlags::LocalShadow;
			if (LightLimitFix::IsLightIgnored(light) || ((light.lightFlags & LightLimitFix::LightFlags::ShadowCaster) && !localShadow)) {
				continue;
			}
			float3 lightDirection = light.positionWS.xyz - positionWS;
			float lightDist = length(lightDirection);
			lightDirection /= max(lightDist, 1e-5);

			float intensityMultiplier = LightLimitFix::GetAttenuation(lightDist, light) * (SharedData::lightLimitFixSettings.EnableLocalShadows ? light.fade : 1.0);
			[branch] if (intensityMultiplier > 1e-5 && localShadow)
				intensityMultiplier *= LightLimitFix::GetLocalShadow(LinearSampler, light.localShadowIndex, positionWS, FrameBuffer::CameraPosAdjust.xyz, lightDirection, false, 0.0);

			lighting += ShadeLight(surface, light.color.xyz * intensityMultiplier, lightDirection);
		}
	}
#	endif
}

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout;

#	if defined(ENVCUBE)
	float2 precipitationOcclusionUV = (input.PrecipitationOcclusionTexCoord.xy * 0.5 + 0.5) * TextureSize.x;
	float precipitationOcclusion = -input.PrecipitationOcclusionTexCoord.z + TexPrecipitationOcclusionTexture.Load(float3(precipitationOcclusionUV, 0)).x;
	float2 underwaterMaskUv = TextureSize.yz * input.Position.xy;
	float underwaterMask = TexUnderwaterMask.Sample(SampUnderwaterMask, underwaterMaskUv).x;
	if (precipitationOcclusion - underwaterMask < 0) {
		discard;
	}
#	endif

#	if defined(REFRACTIVE_RAIN)
	if (SharedData::rainRefractionSettings.Enable) {
		float4 raindropNormal = TexRaindropNormals.Sample(SampSourceTexture, input.RaindropData.xy);
		float alpha = saturate(raindropNormal.w * input.RaindropData.z * (1.0 - SharedData::rainRefractionSettings.MotionTransparency));
		raindropNormal.y = 1.0 - raindropNormal.y;

		// Reconstruct camera-relative worldspace position (camera at origin).
		float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(input.Position.xy * SharedData::BufferDim.zw);
		float4 posCS = float4(2.0 * float2(uv.x, 1.0 - uv.y) - 1.0, input.Position.z, 1.0);
		float4 posWS = mul(FrameBuffer::CameraViewProjInverse, posCS);
		posWS.xyz /= posWS.w;

		// Build worldspace TBN from screen-space derivatives. The billboard is camera-aligned,
		// so dPdx/dPdy lie in the billboard plane along screen X/Y.
		float3 T = normalize(ddx(posWS.xyz));
		float3 N = normalize(cross(T, -ddy(posWS.xyz)));
		float3 B = cross(N, T);
		float3x3 TBN = float3x3(T, B, N);

		float3 normalTS = normalize(raindropNormal.xyz * 2.0 - 1.0);
		float3 normalWS = normalize(mul(normalTS, TBN));

		// Antialias the edges of the drop's covered area, which the billboard now ends at
		float2 dropUV = (input.RaindropData.xy - SharedData::rainRefractionSettings.TextureBounds.xy) /
		                (SharedData::rainRefractionSettings.TextureBounds.zw - SharedData::rainRefractionSettings.TextureBounds.xy);
		float2 edgeCoverage = saturate(min(dropUV, 1.0 - dropUV) / max(fwidth(dropUV), EPSILON_DIVISION));
		alpha *= edgeCoverage.x * edgeCoverage.y;
		if (alpha <= 0.0)
			discard;

		if (frontFace)
			normalWS = -normalWS;

		float3 V = normalize(-posWS.xyz);
		float NdotV = saturate(dot(normalWS, V));
		float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

		float3 reflectDir = reflect(-V, normalWS);
		float3 refractDir = refract(-V, normalWS, 1.0 / 1.33);
		if (dot(refractDir, refractDir) < 1e-4)
			refractDir = -V;

		float3 reflectColor = ColorManagement::SceneToLinear(DynamicCubemaps::EnvReflectionsTexture.SampleLevel(SampSourceTexture, reflectDir, 0).xyz);
		float3 refractColor = ColorManagement::SceneToLinear(DynamicCubemaps::EnvReflectionsTexture.SampleLevel(SampSourceTexture, refractDir, 0).xyz);

		// The env cubemap only carries faint bounce light, so lights are shaded on the drop directly
		ParticleSurface drop = { true, normalWS, V, refractDir };
		float3 directLighting = ShadeLight(drop, GetDirLightColor(posWS.xyz), SharedData::DirLightDirection.xyz);
		AddPointLighting(posWS.xyz, drop, directLighting);

		psout.Color.xyz = ColorManagement::LinearToScene(lerp(refractColor, reflectColor, fresnel) + directLighting);
		psout.Color.w = alpha;
		psout.Normal = float4(0, 1, 0, alpha);
		return psout;
	}
#	endif

	float4 sourceColor = TexSourceTexture.Sample(SampSourceTexture, input.TexCoord0);
	float4 baseColor;
#	if defined(GRAYSCALE_TO_COLOR)
	baseColor = input.Color * sourceColor;
#	else
	baseColor.xyz = input.Color.xyz * Color::Albedo(ColorManagement::TextureToWorking(sourceColor.xyz));
	baseColor.w = input.Color.w * sourceColor.w;
#	endif
#	if defined(GRAYSCALE_TO_COLOR)
	float3 grayScaleColor =
		TexGrayscaleTexture.Sample(SampGrayscaleTexture, float2(sourceColor.y, input.Color.x)).xyz;
	baseColor.xyz = ColorManagement::TextureToWorking(grayScaleColor);
#	endif
#	if defined(GRAYSCALE_TO_ALPHA)
	float grayScaleAlpha =
		TexGrayscaleTexture.Sample(SampGrayscaleTexture, float2(sourceColor.w, input.Color.w)).w;
	baseColor.w = grayScaleAlpha;
#	endif

	float3 propertyColor = 0.0;

	float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(input.Position.xy * SharedData::BufferDim.zw);

	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, input.Position.z, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse, positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

	float3 dirLightColor = GetDirLightColor(positionWS.xyz);
	float3 ambientColor = max(0, SharedData::GetAmbient(float3(0, 0, 1)));
#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		ambientColor = ImageBasedLighting::GetDiffuseIBL(ambientColor, float3(0, 0, -1));
	}
#	endif

	const ParticleSurface unlit = (ParticleSurface)0;
	propertyColor += dirLightColor;
	propertyColor += ambientColor;
	AddPointLighting(positionWS.xyz, unlit, propertyColor);

	psout.Color.xyz = propertyColor * baseColor.xyz;
	psout.Color.w = baseColor.w;
	psout.Normal.w = baseColor.w;
	psout.Normal.xyz = float3(0, 1, 0);

#	if defined(ENABLE_LL)
	if (!Permutation::RenderToUI || (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection)) {
#		if !defined(ENVCUBE)
		if (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::SourceAlphaBlend)
#		endif
			psout.Color.w = pow(saturate(psout.Color.w), TransferFunctions::GAME_GAMMA);
	}
	if (Permutation::RenderToUI && !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection))
		psout.Color.rgb = ColorManagement::WorkingToUI(psout.Color.rgb);
#	endif
	return psout;
}
#endif
