#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#define DEFERRED

#ifdef GRASS_LIGHTING
#	define GRASS
#endif  // GRASS_LIGHTING

#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
	float4 Normal: NORMAL0;
	float4 Color: COLOR0;
	float4 InstanceData1: TEXCOORD4;
	float4 InstanceData2: TEXCOORD5;
	float4 InstanceData3: TEXCOORD6;
	float4 InstanceData4: TEXCOORD7;
};

#ifdef GRASS_LIGHTING
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float4 Color: COLOR0;
	float VertexMult: COLOR1;
	float3 TexCoord: TEXCOORD0;
	float3 ViewSpacePosition: TEXCOORD1;
#	if defined(RENDER_DEPTH)
	float2 Depth: TEXCOORD2;
#	endif  // RENDER_DEPTH
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float4 VertexNormal: POSITION4;
};
#else
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float4 Color: COLOR0;
	float VertexMult: COLOR1;
	float3 TexCoord: TEXCOORD0;
	float4 AmbientColor: TEXCOORD1;
	float3 ViewSpacePosition: TEXCOORD2;
#	if defined(RENDER_DEPTH)
	float2 Depth: TEXCOORD3;
#	endif  // RENDER_DEPTH
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
};
#endif

cbuffer PerGeometry : register(
#ifdef VSHADER
						  b2
#else
						  b3
#endif
					  )
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 WorldView : packoffset(c4);
	row_major float4x4 World : packoffset(c8);
	row_major float4x4 PreviousWorld : packoffset(c12);
	float4 FogNearColor : packoffset(c16);
	float3 WindVector : packoffset(c17);
	float WindTimer : packoffset(c17.w);
	float3 DirLightDirection : packoffset(c18);
	float PreviousWindTimer : packoffset(c18.w);
	float3 DirLightColor : packoffset(c19);
	float AlphaParam1 : packoffset(c19.w);
	float3 AmbientColor : packoffset(c20);
	float AlphaParam2 : packoffset(c20.w);
	float3 ScaleMask : packoffset(c21);
	float ShadowClampValue : packoffset(c21.w);
}

#ifdef VSHADER

#	ifdef GRASS_COLLISION
#		include "GrassCollision\\GrassCollision.hlsli"
#	endif  // GRASS_COLLISION

cbuffer cb7 : register(b7)
{
	float4 cb7[1];
}

cbuffer cb8 : register(b8)
{
	float4 cb8[240];
}

// Calculate wind displacement for a grass vertex
float3 CalculateWindDisplacement(VS_INPUT input, float windTimer)
{
	float windAngle = 0.4 * ((input.InstanceData1.x + input.InstanceData1.y) * -0.0078125 + windTimer);
	float windAngleSin, windAngleCos;
	sincos(windAngle, windAngleSin, windAngleCos);

	float windTmp3 = 0.2 * cos(Math::PI * windAngleCos);
	float windTmp1 = sin(Math::PI * windAngleSin);
	float windTmp2 = sin(Math::TAU * windAngleSin);
	float windPower = WindVector.z * (((windTmp1 + windTmp2) * 0.3 + windTmp3) *
										 (0.5 * (input.Color.w * input.Color.w)));

	return float3(WindVector.xy, 0) * windPower;
}

#	ifdef GRASS_LIGHTING
float4 GetMSPosition(VS_INPUT input, float3x3 world3x3)
#	else
float4 GetMSPosition(VS_INPUT input)
#	endif
{
	float3 inputPosition = input.Position.xyz * (input.InstanceData4.yyy * ScaleMask.xyz + float3(1, 1, 1));

#	ifdef GRASS_LIGHTING
	float3 transformedPosition = mul(world3x3, inputPosition);
	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + transformedPosition;
#	else
	float3 instancePosition;
	instancePosition.z = dot(
		float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w), inputPosition);
	instancePosition.x = dot(input.InstanceData2.xyz, inputPosition);
	instancePosition.y = dot(input.InstanceData3.xyz, inputPosition);

	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + instancePosition;
#	endif
	msPosition.w = 1;

	return msPosition;
}

#	ifdef GRASS_LIGHTING
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float3x3 world3x3 = float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));

	float4 msPosition = GetMSPosition(input, world3x3);

	float3 windDisplacement = CalculateWindDisplacement(input, WindTimer);
	float3 previousWindDisplacement = CalculateWindDisplacement(input, PreviousWindTimer);

#		ifdef GRASS_COLLISION
	float3 displacement, previousDisplacement;
	GrassCollision::GetDisplacedPosition(input, msPosition.xyz, displacement, previousDisplacement);
	msPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	msPosition.xyz += windDisplacement;

	float4 projSpacePosition = mul(WorldViewProj, msPosition);
	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);

	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);

	// Note: input.Color.w is used for wind speed
	vsout.Color.xyz = input.Color.xyz;
	vsout.Color.w = distanceFade * perInstanceFade;
	vsout.VertexMult = input.InstanceData1.w;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.ViewSpacePosition = mul(WorldView, msPosition).xyz;
	vsout.WorldPosition = mul(World, msPosition);

	float4 previousMsPosition = GetMSPosition(input, world3x3);

#		ifdef GRASS_COLLISION
	previousMsPosition.xyz += previousDisplacement;
#		endif  // GRASS_COLLISION

	previousMsPosition.xyz += previousWindDisplacement;

	vsout.PreviousWorldPosition = mul(PreviousWorld, previousMsPosition);

	// Vertex normal needs to be transformed to world-space for lighting calculations.
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
	vsout.VertexNormal.w = input.Color.w;

	return vsout;
}
#	else
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 msPosition = GetMSPosition(input);

	float3 windDisplacement = CalculateWindDisplacement(input, WindTimer);
	float3 previousWindDisplacement = CalculateWindDisplacement(input, PreviousWindTimer);

#		ifdef GRASS_COLLISION
	float3 displacement, previousDisplacement;
	GrassCollision::GetDisplacedPosition(input, msPosition.xyz, displacement, previousDisplacement);
	msPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	msPosition.xyz += windDisplacement;

	float4 projSpacePosition = mul(WorldViewProj, msPosition);
	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	float dirLightAngle = dot(DirLightDirection.xyz, instanceNormal);
	float3 diffuseMultiplier = input.InstanceData1.www * input.Color.xyz;

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);

	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);

	vsout.Color.xyz = input.Color.xyz;
	vsout.Color.w = distanceFade * perInstanceFade;
	vsout.VertexMult = input.InstanceData1.w;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.AmbientColor.xyz = input.InstanceData1.www * (AmbientColor.xyz * input.Color.xyz);
	vsout.AmbientColor.w = ShadowClampValue;

	vsout.ViewSpacePosition = mul(WorldView, msPosition).xyz;
	vsout.WorldPosition = mul(World, msPosition);

	float4 previousMsPosition = GetMSPosition(input);

#		ifdef GRASS_COLLISION
	previousMsPosition.xyz += previousDisplacement;
#		endif  // GRASS_COLLISION

	previousMsPosition.xyz += previousWindDisplacement;

	vsout.PreviousWorldPosition = mul(PreviousWorld, previousMsPosition);

	return vsout;
}

#	endif

#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#ifdef GRASS_LIGHTING
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS: SV_Target0;
#	else
	float4 Diffuse: SV_Target0;
	float2 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
#		if defined(TRUE_PBR)
	float4 Reflectance: SV_Target5;
#		endif  // TRUE_PBR
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif      // RENDER_DEPTH
};
#else
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS: SV_Target0;
#	else
	float4 Diffuse: SV_Target0;
	float2 MotionVectors: SV_Target1;
	float4 Normal: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif
};
#endif

#ifdef PSHADER
SamplerState SampBaseSampler : register(s0);
SamplerState SampShadowMaskSampler : register(s1);

#	ifdef GRASS_LIGHTING
#		if defined(TRUE_PBR)
SamplerState SampNormalSampler : register(s2);
SamplerState SampRMAOSSampler : register(s3);
SamplerState SampSubsurfaceSampler : register(s4);
#		endif  // TRUE_PBR
#	endif      // GRASS_LIGHTING

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexShadowMaskSampler : register(t1);

cbuffer PerFrame : register(b0)
{
	float4 cb0_1[2] : packoffset(c0);
	float4 VPOSOffset : packoffset(c2);
	float4 cb0_2[7] : packoffset(c3);
}

#	ifdef GRASS_LIGHTING
#		if defined(TRUE_PBR)
Texture2D<float4> TexNormalSampler : register(t2);
Texture2D<float4> TexRMAOSSampler : register(t3);
Texture2D<float4> TexSubsurfaceSampler : register(t4);
#		endif  // TRUE_PBR

#	endif  // GRASS_LIGHTING

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

#	if defined(SCREEN_SPACE_SHADOWS)
#		include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#	endif

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#		include "InverseSquareLighting/InverseSquareLighting.hlsli"
#	endif

#	define SampColorSampler SampBaseSampler

#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(EXP_HEIGHT_FOG)
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

#	define LinearSampler SampBaseSampler

#	include "Common/ShadowSampling.hlsli"

#	ifdef GRASS_LIGHTING
#		if defined(TRUE_PBR)

cbuffer PerMaterial : register(b1)
{
	uint PBRFlags : packoffset(c0.x);
	float3 PBRParams1 : packoffset(c0.y);  // roughness scale, specular level
	float4 PBRParams2 : packoffset(c1);    // subsurface color, subsurface opacity
};

#			include "Common/PBR.hlsli"

#		endif  // TRUE_PBR

#		include "GrassLighting/GrassLighting.hlsli"

float GetSoftLightMultiplier(float angle, float rolloff)
{
	float softLight = saturate((rolloff + angle) / (1 + rolloff));
	float arg1 = (softLight * softLight) * (3 - 2 * softLight);
	float clampedAngle = saturate(angle);
	float arg2 = (clampedAngle * clampedAngle) * (3 - 2 * clampedAngle);
	return saturate(arg1 - arg2);
}

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout = (PS_OUTPUT)0;

#		if !defined(TRUE_PBR)
	float x;
	float y;
	TexBaseSampler.GetDimensions(x, y);

	float3 complexTest = TexBaseSampler.Load(int3(0, int(y) - 1, 0)).xyz * 2.0 - 1.0;
	float complexLength = length(complexTest);
	bool complex = abs(complexLength - 1.0) < SharedData::grassLightingSettings.ComplexGrassThreshold;
#		endif  // !TRUE_PBR

	float4 baseColor;
#		if !defined(TRUE_PBR)
	if (complex) {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, input.TexCoord.y * 0.5), SharedData::MipBias);
	} else
#		endif  // !TRUE_PBR
	{
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	}

	baseColor.xyz = Color::Diffuse(baseColor.xyz);

#		if defined(RENDER_DEPTH)
	float diffuseAlpha = input.Color.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else
	if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;

#			if !defined(TRUE_PBR)
	float4 specColor = complex ? TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, 0.5 + input.TexCoord.y * 0.5), SharedData::MipBias) : 1;
#			else
	float4 specColor = TexNormalSampler.SampleBias(SampNormalSampler, input.TexCoord.xy, SharedData::MipBias);
#			endif

	psout.MotionVectors = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float3 normal = normalize(input.VertexNormal.xyz);

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	// Swaps direction of the backfaces otherwise they seem to get lit from the wrong direction.
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::GrassSphereNormal))
		if (!frontFace)
			normal = -normal;

	float3x3 tbn = 0;

#			if !defined(TRUE_PBR)
	if (complex)
#			endif  // !TRUE_PBR
	{
		float3 normalColor = GrassLighting::TransformNormal(specColor.xyz);
		// world-space -> tangent-space -> world-space.
		// This is because we don't have pre-computed tangents.
		tbn = GrassLighting::CalculateTBN(normal, -input.WorldPosition.xyz, input.TexCoord.xy);
		normal = normalize(mul(normalColor, tbn));
	}

#			if !defined(TRUE_PBR)
	if (!complex || SharedData::grassLightingSettings.OverrideComplexGrassSettings)
		baseColor.xyz *= SharedData::grassLightingSettings.BasicGrassBrightness;
#			endif  // !TRUE_PBR

#			if defined(TRUE_PBR)
	float4 rawRMAOS = TexRMAOSSampler.SampleBias(SampRMAOSSampler, input.TexCoord.xy, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.y);

	PBR::SurfaceProperties pbrSurfaceProperties = PBR::InitSurfaceProperties();

	pbrSurfaceProperties.Roughness = saturate(rawRMAOS.x);
	pbrSurfaceProperties.Metallic = saturate(rawRMAOS.y);
	pbrSurfaceProperties.AO = rawRMAOS.z;
	pbrSurfaceProperties.F0 = lerp(saturate(rawRMAOS.w), baseColor.xyz, pbrSurfaceProperties.Metallic);

	baseColor.xyz *= 1 - pbrSurfaceProperties.Metallic;

	pbrSurfaceProperties.BaseColor = baseColor.xyz;

	pbrSurfaceProperties.SubsurfaceColor = PBRParams2.xyz;
	pbrSurfaceProperties.Thickness = PBRParams2.w;
	[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
	{
		float4 sampledSubsurfaceProperties = TexSubsurfaceSampler.Sample(SampSubsurfaceSampler, input.TexCoord.xy);
		pbrSurfaceProperties.SubsurfaceColor *= sampledSubsurfaceProperties.xyz;
		pbrSurfaceProperties.Thickness *= sampledSubsurfaceProperties.w;
	}

	float3 specularColorPBR = 0;
	float3 transmissionColor = 0;
#			endif  // TRUE_PBR

	float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;
	float3 dirLightColorMultiplier = 1;

#			if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		dirLightColor *= ExponentialHeightFog::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#			endif

	float dirLightAngle = dot(normal, SharedData::DirLightDirection.xyz);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (!SharedData::InInterior)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirDetailedShadow = 1.0;

	if (!SharedData::InInterior)
		dirDetailedShadow *= shadowColor.x;

#			if defined(SCREEN_SPACE_SHADOWS)
	if (!SharedData::InInterior && dirLightAngle >= 0.0)
		dirDetailedShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise);
#			endif  // SCREEN_SPACE_SHADOWS

	float3 diffuseColor = 0;
	float3 specularColor = 0;

	float3 lightsDiffuseColor = 0;
	float3 lightsSpecularColor = 0;

#			if defined(TRUE_PBR)
	{
		PBR::LightProperties lightProperties = PBR::InitLightProperties(SharedData::DirLightColor.xyz, dirLightColorMultiplier * dirDetailedShadow, 1);
		float3 dirDiffuseColor, coatDirDiffuseColor, dirTransmissionColor, dirSpecularColor;
		PBR::GetDirectLightInput(dirDiffuseColor, coatDirDiffuseColor, dirTransmissionColor, dirSpecularColor, normal, normal, viewDirection, viewDirection, DirLightDirection, DirLightDirection, lightProperties, pbrSurfaceProperties, tbn, input.TexCoord.xy);
		lightsDiffuseColor += dirDiffuseColor;
		transmissionColor += dirTransmissionColor;
		specularColorPBR += dirSpecularColor;
	}
#			else
	dirLightColor *= dirLightColorMultiplier;

	float softLightRolloff = saturate(input.VertexNormal.w * 10.0) * SharedData::grassLightingSettings.SubsurfaceScatteringAmount * 2.0;

	lightsDiffuseColor += dirLightColor * dirDetailedShadow * saturate(dirLightAngle) * Color::VanillaNormalization();

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	vertexColor /= max(vertexAO, EPSILON_DIVISION);

#				if defined(SKYLIGHTING)
	float3 positionMSSkylight = input.WorldPosition.xyz;
	float skylightingDiffuse = Skylighting::GetVertexSkylightingDiffuse(positionMSSkylight, normal, vertexAO);
#				endif  // SKYLIGHTING

	float3 albedo = baseColor.xyz * vertexColor;

	float3 subsurfaceColor = dirLightColor * dirDetailedShadow * (GetSoftLightMultiplier(dirLightAngle, softLightRolloff)) * Color::VanillaNormalization();

	if (complex)
		lightsSpecularColor += dirDetailedShadow * GrassLighting::GetLightSpecularInput(SharedData::DirLightDirection.xyz, viewDirection, normal, dirLightColor, SharedData::grassLightingSettings.Glossiness) * Color::VanillaNormalization();
#			endif

#			if defined(LIGHT_LIMIT_FIX)
	uint clusterIndex = 0;
	uint lightCount = 0;

	if (LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
				LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];

				float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);

#				if defined(ISL)
				float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
				if (intensityMultiplier < 1e-5)
					continue;
#				else
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#				endif

				float3 lightColor = Color::PointLight(light.color.xyz) * intensityMultiplier * light.fade;
				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				float3 normalizedLightDirection = normalize(lightDirection);

#				if defined(TRUE_PBR)
				{
					PBR::LightProperties lightProperties = PBR::InitLightProperties(lightColor, lightShadow, 1);
					float3 pointDiffuseColor, coatDirDiffuseColor, pointTransmissionColor, pointSpecularColor;
					PBR::GetDirectLightInput(pointDiffuseColor, coatDirDiffuseColor, pointTransmissionColor, pointSpecularColor, normal, normal, viewDirection, viewDirection, normalizedLightDirection, normalizedLightDirection, lightProperties, pbrSurfaceProperties, tbn, input.TexCoord.xy);
					lightsDiffuseColor += pointDiffuseColor;
					transmissionColor += pointTransmissionColor;
					specularColorPBR += pointSpecularColor;
				}
#				else
				lightColor *= lightShadow;

				float lightAngle = dot(normal, normalizedLightDirection);
				float lightNoL = dot(normalizedLightDirection.xyz, viewDirection);
				float3 lightDiffuseColor;

				lightDiffuseColor = lightColor * saturate(lightAngle);

				subsurfaceColor += lightColor * GetSoftLightMultiplier(lightAngle, softLightRolloff) * Color::VanillaNormalization();

				lightsDiffuseColor += lightDiffuseColor * Color::VanillaNormalization();

				if (complex)
					lightsSpecularColor += GrassLighting::GetLightSpecularInput(normalizedLightDirection, viewDirection, normal, lightColor, SharedData::grassLightingSettings.Glossiness) * Color::VanillaNormalization();
#				endif
			}
		}
	}
#			endif  // LIGHT_LIMIT_FIX

	diffuseColor += lightsDiffuseColor;

#			if defined(TRUE_PBR)
	float3 indirectDiffuseLobeWeight, indirectSpecularLobeWeight;
	PBR::GetIndirectLobeWeights(indirectDiffuseLobeWeight, indirectSpecularLobeWeight, normal, normal, viewDirection, baseColor.xyz, pbrSurfaceProperties);

	diffuseColor.xyz += transmissionColor;
	specularColor.xyz += specularColorPBR;
	specularColor.xyz = Color::IrradianceToGamma(specularColor.xyz);
	diffuseColor.xyz = Color::IrradianceToGamma(diffuseColor.xyz);
#			else

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));

#				if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
#					if defined(SKYLIGHTING)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -normal, skylightingDiffuse);
#					else
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
#					endif
	}
#				endif

	diffuseColor += directionalAmbientColor;
	diffuseColor += subsurfaceColor * albedo;
	diffuseColor *= albedo;

	directionalAmbientColor *= albedo;

#				if defined(SKYLIGHTING)
#					if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#					endif
	{
		Skylighting::ApplySkylighting(diffuseColor, directionalAmbientColor, albedo, skylightingDiffuse);
	}
#				endif

	specularColor += lightsSpecularColor;
	specularColor *= specColor.w * SharedData::grassLightingSettings.SpecularStrength;
#			endif

#			if defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 0) {
			diffuseColor.xyz = Color::TurboColormap(0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			diffuseColor.xyz = Color::TurboColormap(0);
		} else {
			diffuseColor.xyz = Color::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
		}
	} else {
		psout.Diffuse = float4(diffuseColor, 1);
	}
#			else
	psout.Diffuse.xyz = diffuseColor;
#			endif

	float3 normalVS = normalize(FrameBuffer::WorldToView(normal, false));
#			if defined(TRUE_PBR)
	psout.Albedo = float4(Color::IrradianceToGamma(indirectDiffuseLobeWeight), 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), 1 - pbrSurfaceProperties.Roughness, 1);
	psout.Reflectance = float4(indirectSpecularLobeWeight, 1);
#			else
	psout.Albedo = float4(albedo, 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), specColor.w, 1);
#			endif

	psout.Specular = float4(specularColor, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 0);
#		endif
	return psout;
}
#	else
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);

#		if defined(RENDER_DEPTH)
	float diffuseAlpha = input.Color.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else
	if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (!SharedData::InInterior)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirDetailedShadow = 1.0;

	if (!SharedData::InInterior)
		dirDetailedShadow = shadowColor.x;

#			if defined(SCREEN_SPACE_SHADOWS)
	if (!SharedData::InInterior)
		dirDetailedShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise);
#			endif  // SCREEN_SPACE_SHADOWS

	float3 diffuseColor = dirLightColor * dirDetailedShadow;

#			if defined(LIGHT_LIMIT_FIX)
	uint clusterIndex = 0;
	uint lightCount = 0;

	if (LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
				LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];

				float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);

#				if defined(ISL)
				float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
				if (intensityMultiplier < 1e-5)
					continue;
#				else
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#				endif

				const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
				float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * intensityMultiplier * light.fade;

				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				lightColor *= lightShadow;

				diffuseColor += lightColor;
			}
		}
	}
#			endif  // LIGHT_LIMIT_FIX

	float3 ddx = ddx_coarse(input.WorldPosition);
	float3 ddy = ddy_coarse(input.WorldPosition);
	float3 normal = -normalize(cross(ddx, ddy));

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	vertexColor /= max(vertexAO, EPSILON_DIVISION);

#			if defined(SKYLIGHTING)
	float3 positionMSSkylight = input.WorldPosition.xyz;
	float skylightingDiffuse = Skylighting::GetVertexSkylightingDiffuse(positionMSSkylight, normal, vertexAO);
#			endif  // SKYLIGHTING

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));

#			if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
#				if defined(SKYLIGHTING)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -normal, skylightingDiffuse);
#				else
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
#				endif
	}
#			endif

	diffuseColor += directionalAmbientColor;

	float3 albedo = baseColor.xyz * vertexColor;

	diffuseColor *= albedo;
	directionalAmbientColor *= albedo;

#			if defined(SKYLIGHTING)
#				if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#				endif
	{
		Skylighting::ApplySkylighting(diffuseColor, directionalAmbientColor, albedo, skylightingDiffuse);
	}
#			endif

	psout.Diffuse.xyz = diffuseColor;

	psout.Diffuse.w = 1;

	psout.MotionVectors = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
	psout.Normal.xy = GBuffer::EncodeNormal(FrameBuffer::WorldToView(normal, false));
	psout.Normal.zw = 0;

	psout.Albedo = float4(albedo, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 0);
#		endif

	return psout;
}
#	endif

#endif  // PSHADER
