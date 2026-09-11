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

// Both paths use upstream's vertex inputs. Only outputs consumed by this pass are interpolated.
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float2 TexCoord: TEXCOORD0;
#if defined(RENDER_DEPTH)
	float Fade: TEXCOORD2;
#	ifndef GRASS_OPTIMIZATIONS
	float2 Depth: TEXCOORD4;
#	endif
#else
	// Fade shares the otherwise unused color alpha component.
	float4 Color: COLOR0;
	float3 WorldPosition: POSITION1;
	float3 PreviousWorldPosition: POSITION2;
#	ifdef GRASS_LIGHTING
	float4 VertexNormal: POSITION4;
#	else
	float DirLightAngle: TEXCOORD1;
#		ifndef GRASS_OPTIMIZATIONS
	// Preserve the engine's WorldView transform for standard-path geometric normals.
	float3 ViewSpacePosition: TEXCOORD3;
#		endif
#	endif
#endif
#ifdef GRASS_OPTIMIZATIONS
	nointerpolation float IsComplex: TEXCOORD8;
#	if !defined(RENDER_DEPTH)
	nointerpolation float IsFar: TEXCOORD9;
	// 0 = full mesh, 1 = middle LOD mesh, 2 = far LOD mesh.
	nointerpolation float LodTier: TEXCOORD10;
#	endif
#endif
};

cbuffer PerGeometry : register(b2)
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

#	ifdef GRASS_OPTIMIZATIONS
// Two per instance: [0] = origin.xyz + isComplex, [1] = windCur, windPrev, fade, packed flags.
StructuredBuffer<float4> InstanceExtras : register(t2);
#	else
cbuffer cb7 : register(b7)
{
	float4 cb7[1];
}

cbuffer cb8 : register(b8)
{
	float4 cb8[240];
}
#	endif

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

#	ifdef GRASS_OPTIMIZATIONS
// Captured instances retain upstream's vertex input layout.
VS_OUTPUT main(VS_INPUT input, uint instanceID : SV_InstanceID)
{
	VS_OUTPUT vsout = (VS_OUTPUT)0;

	const float4 e0 = InstanceExtras[instanceID * 2 + 0];
	const float4 e1 = InstanceExtras[instanceID * 2 + 1];
	vsout.IsComplex = e0.w;
	vsout.TexCoord = input.TexCoord.xy;

	// e1.w packs 4.0 per LOD tier, 2.0 = far and 1.0 = in collision range.
	const float lodTier = floor(e1.w * 0.25);
	const float packedFlags = e1.w - 4.0 * lodTier;
	const float isFarFlag = (packedFlags >= 2.0) ? 1.0 : 0.0;
	const float collisionFlag = packedFlags - 2.0 * isFarFlag;

#		ifdef GRASS_LIGHTING
	float3x3 world3x3 = float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));
	float4 msPosition = GetMSPosition(input, world3x3);
#		else
	float4 msPosition = GetMSPosition(input);
#		endif
	msPosition.xyz += e0.xyz;

#		if !defined(RENDER_DEPTH)
	float4 previousMsPosition = msPosition;
#		endif

#		ifdef GRASS_COLLISION
	[branch] if (collisionFlag > 0.5)
	{
		// Captured instances already include the cell origin; do not apply World a second time.
		const float3 collisionPos = msPosition.xyz - FrameBuffer::CameraPosAdjust.xyz;
		const float3 collisionCentre = input.InstanceData1.xyz + e0.xyz - FrameBuffer::CameraPosAdjust.xyz;
		float3 displacement, previousDisplacement;
		GrassCollision::GetDisplacedPosition(input, collisionPos, collisionCentre, displacement, previousDisplacement);
		msPosition.xyz += displacement;
#			if !defined(RENDER_DEPTH)
		previousMsPosition.xyz += previousDisplacement;
#			endif
	}
#		endif

	const float vertexTerm = WindVector.z * (0.5 * (input.Color.w * input.Color.w));
	msPosition.xyz += float3(WindVector.xy, 0) * (e1.x * vertexTerm);
	const float3 eyeRel = msPosition.xyz - FrameBuffer::CameraPosAdjust.xyz;
	const float4 projSpacePosition = mul(FrameBuffer::CameraViewProj, float4(eyeRel, 1.0));
	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Fade = e1.z;
#		else
	previousMsPosition.xyz += float3(WindVector.xy, 0) * (e1.y * vertexTerm);
	vsout.Color = float4(input.InstanceData1.www * input.Color.xyz, e1.z);
#			ifndef GRASS_LIGHTING
	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	vsout.DirLightAngle = saturate(dot(DirLightDirection.xyz, instanceNormal));
#			endif
	vsout.WorldPosition = eyeRel;
	vsout.PreviousWorldPosition = previousMsPosition.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
	vsout.IsFar = isFarFlag;
	vsout.LodTier = lodTier;
#			ifdef GRASS_LIGHTING
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
	vsout.VertexNormal.w = input.Color.w;
#			endif
#		endif

	return vsout;
}
#	else  // GRASS_OPTIMIZATIONS

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

#		ifdef GRASS_LIGHTING
	float3x3 world3x3 = float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));
	float4 msPosition = GetMSPosition(input, world3x3);
#		else
	float4 msPosition = GetMSPosition(input);
#		endif

#		if !defined(RENDER_DEPTH)
	// Save the undisplaced position instead of repeating the instance transform.
	float4 previousMsPosition = msPosition;
#		endif

#		ifdef GRASS_COLLISION
	float3 displacement, previousDisplacement;
	GrassCollision::GetDisplacedPosition(input, msPosition.xyz, displacement, previousDisplacement);
	msPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	msPosition.xyz += CalculateWindDisplacement(input, WindTimer);

	float4 projSpacePosition = mul(WorldViewProj, msPosition);
	vsout.HPosition = projSpacePosition;
	vsout.TexCoord = input.TexCoord.xy;

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);
	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
	vsout.Fade = distanceFade * perInstanceFade;
#		else
	vsout.Color = float4(input.InstanceData1.www * input.Color.xyz, distanceFade * perInstanceFade);
	vsout.WorldPosition = mul(World, msPosition).xyz;

#			ifdef GRASS_COLLISION
	previousMsPosition.xyz += previousDisplacement;
#			endif  // GRASS_COLLISION
	previousMsPosition.xyz += CalculateWindDisplacement(input, PreviousWindTimer);
	vsout.PreviousWorldPosition = mul(PreviousWorld, previousMsPosition).xyz;

#			ifdef GRASS_LIGHTING
	// Vertex normal needs to be transformed to world-space for lighting calculations.
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
	vsout.VertexNormal.w = input.Color.w;
#			else
	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	vsout.DirLightAngle = saturate(dot(DirLightDirection.xyz, instanceNormal));
	vsout.ViewSpacePosition = mul(WorldView, msPosition).xyz;
#			endif
#		endif

	return vsout;
}

#	endif  // GRASS_OPTIMIZATIONS

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
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif  // RENDER_DEPTH
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

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexShadowMaskSampler : register(t1);

cbuffer PerFrame : register(b0)
{
	float4 cb0_1[2] : packoffset(c0);
	float4 VPOSOffset : packoffset(c2);
	float4 cb0_2[7] : packoffset(c3);
}

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

#	if defined(SKYLIGHTING)
#		define SKYLIGHTING_SHADOW_VIS
#	endif

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
#		include "GrassLighting/GrassLighting.hlsli"

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout = (PS_OUTPUT)0;

#		if defined(SKYLIGHTING_SHADOW_VIS)
	float skylightingShadowVisibility = 1.0;
#		endif

#		ifdef GRASS_OPTIMIZATIONS
	bool complex = input.IsComplex > 0.5;
#		else
	float x;
	float y;
	TexBaseSampler.GetDimensions(x, y);

	float3 complexTest = TexBaseSampler.Load(int3(0, int(y) - 1, 0)).xyz * 2.0 - 1.0;
	float complexLength = length(complexTest);
	bool complex = abs(complexLength - 1.0) < SharedData::grassLightingSettings.ComplexGrassThreshold;
#		endif

#		if defined(RENDER_DEPTH)
	// Alpha is the only texture channel needed here; select the atlas half without a sample branch.
	const float2 alphaUV = float2(input.TexCoord.x, input.TexCoord.y * (complex ? 0.5 : 1.0));
	const float baseAlpha = TexBaseSampler.SampleBias(SampBaseSampler, alphaUV, SharedData::MipBias).w;
	const float diffuseAlpha = input.Fade * baseAlpha;
	if ((diffuseAlpha - AlphaTestRefRS) < 0)
		discard;

#			ifdef GRASS_OPTIMIZATIONS
	// The optimized main-view pass uses the rasterizer's depth directly, with no extra interpolator.
	psout.PS.xyz = input.HPosition.zzz;
#			else
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
#			endif
	psout.PS.w = diffuseAlpha;
#		else
	float4 baseColor;
	if (complex) {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, input.TexCoord.y * 0.5), SharedData::MipBias);
	} else {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	}

#			if defined(DO_ALPHA_TEST)
	float diffuseAlpha = input.Color.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#			endif

	baseColor.xyz = Color::Diffuse(baseColor.xyz);

	if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;

#			ifdef GRASS_OPTIMIZATIONS
	// Keep the atlas selection above independent of the distant-detail cutoff.
	const bool complexDetail = complex && input.IsFar <= 0.5;
	float4 specColor = complexDetail ? TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, 0.5 + input.TexCoord.y * 0.5), SharedData::MipBias) : 1;
#			else
	float4 specColor = complex ? TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, 0.5 + input.TexCoord.y * 0.5), SharedData::MipBias) : 1;
#			endif

	psout.MotionVectors = MotionBlur::GetSSMotionVector(float4(input.WorldPosition, 1), float4(input.PreviousWorldPosition, 1));

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

#			ifdef GRASS_OPTIMIZATIONS
	if (complexDetail)
#			else
	if (complex)
#			endif
	{
		float3 normalColor = GrassLighting::TransformNormal(specColor.xyz);
		// world-space -> tangent-space -> world-space.
		// This is because we don't have pre-computed tangents.
		tbn = GrassLighting::CalculateTBN(normal, -input.WorldPosition.xyz, input.TexCoord.xy);
		normal = normalize(mul(normalColor, tbn));
	}

	if (!complex || SharedData::grassLightingSettings.OverrideComplexGrassSettings)
		baseColor.xyz *= SharedData::grassLightingSettings.BasicGrassBrightness;

#			ifdef GRASS_OPTIMIZATIONS
	const float lodBrightness = input.LodTier > 1.5 ? SharedData::grassLightingSettings.FarLODBrightness : SharedData::grassLightingSettings.MidLODBrightness;
	baseColor.xyz *= lerp(1.0, lodBrightness, saturate(input.LodTier));
#			endif

	float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;
	float3 dirLightColorMultiplier = 1;

#			if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		dirLightColor *= ExponentialHeightFog::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#			endif

	float dirNdotL = dot(normal, SharedData::DirLightDirection.xyz);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (!SharedData::InInterior)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirDetailedShadow = 1.0;

	if (!SharedData::InInterior)
		dirDetailedShadow *= shadowColor.x;
	float dirTransmissionShadow = dirDetailedShadow;

#			if defined(SCREEN_SPACE_SHADOWS)
#				ifdef GRASS_OPTIMIZATIONS
	if (!SharedData::InInterior && input.IsFar <= 0.5)
#				else
	if (!SharedData::InInterior)
#				endif
	{
		float2 screenSpaceShadows = ScreenSpaceShadows::GetScreenSpaceShadows(input.HPosition.xyz, screenUV, screenNoise);
		if (dirNdotL >= 0.0)
			dirDetailedShadow *= screenSpaceShadows.x;
		dirTransmissionShadow *= dirNdotL >= 0.0 ? screenSpaceShadows.x : screenSpaceShadows.y;
	}
#			endif  // SCREEN_SPACE_SHADOWS

	float3 diffuseColor = 0;
	float3 specularColor = 0;

	float3 lightsDiffuseColor = 0;
	float3 lightsSpecularColor = 0;

	dirLightColor *= dirLightColorMultiplier;

	lightsDiffuseColor += dirLightColor * dirDetailedShadow * saturate(dirNdotL) * Color::VanillaNormalization();

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	vertexColor /= max(vertexAO, EPSILON_DIVISION);

#			if defined(SKYLIGHTING)
	float3 positionMSSkylight = input.WorldPosition.xyz;
	sh2 skylightingSH = Skylighting::Sample(positionMSSkylight, normal
#				if defined(SKYLIGHTING_SHADOW_VIS)
		,
		skylightingShadowVisibility
#				endif
	);
	float skylightingDiffuse = Skylighting::GetSkylightingDiffuse(skylightingSH, positionMSSkylight, normal, vertexAO);
#			endif  // SKYLIGHTING

	float3 albedo = baseColor.xyz * vertexColor;
	float3 transmissionTint = GrassLighting::GetTransmissionTint(albedo);

	float dirVdotL = dot(viewDirection, SharedData::DirLightDirection.xyz);
	float3 transmissionRadiance = dirLightColor * dirTransmissionShadow *
	                              GrassLighting::GetTransmissionFactor(dirNdotL, dirVdotL, SharedData::grassLightingSettings.SubsurfaceScatteringAmount) *
	                              Color::VanillaNormalization();

#			ifdef GRASS_OPTIMIZATIONS
	if (complexDetail)
#			else
	if (complex)
#			endif
		lightsSpecularColor += dirDetailedShadow * GrassLighting::GetLightSpecularInput(SharedData::DirLightDirection.xyz, viewDirection, normal, dirLightColor, SharedData::grassLightingSettings.Glossiness) * Color::VanillaNormalization();

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

				lightColor *= lightShadow;

				float NdotL = dot(normal, normalizedLightDirection);
				float3 lightDiffuseColor;

				lightDiffuseColor = lightColor * saturate(NdotL);

				float VdotL = dot(viewDirection, normalizedLightDirection);
				transmissionRadiance += lightColor *
				                        GrassLighting::GetTransmissionFactor(NdotL, VdotL, SharedData::grassLightingSettings.SubsurfaceScatteringAmount) *
				                        Color::VanillaNormalization();

				lightsDiffuseColor += lightDiffuseColor * Color::VanillaNormalization();

#				ifdef GRASS_OPTIMIZATIONS
				if (complexDetail)
#				else
				if (complex)
#				endif
					lightsSpecularColor += GrassLighting::GetLightSpecularInput(normalizedLightDirection, viewDirection, normal, lightColor, SharedData::grassLightingSettings.Glossiness) * Color::VanillaNormalization();
			}
		}
	}
#			endif  // LIGHT_LIMIT_FIX

	diffuseColor += lightsDiffuseColor;

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));

#			if defined(IBL)
	if (SharedData::iblSettings.EnableIBL)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
#			endif

	diffuseColor += directionalAmbientColor;
	diffuseColor *= albedo;

	directionalAmbientColor *= albedo;

#			if defined(SKYLIGHTING)
	Skylighting::ApplySkylighting(diffuseColor, directionalAmbientColor, albedo, skylightingDiffuse);
#			endif

	diffuseColor += transmissionRadiance * transmissionTint;

	specularColor += lightsSpecularColor;
	specularColor *= specColor.w * SharedData::grassLightingSettings.SpecularStrength;

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
	psout.Diffuse.xyz = FogNearColor.w * diffuseColor;
#			endif

	float3 normalVS = normalize(FrameBuffer::WorldToView(normal, false));
	psout.Albedo = float4(albedo, 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), specColor.w, 1);

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

#		if defined(SKYLIGHTING_SHADOW_VIS)
	float skylightingShadowVisibility = 1.0;
#		endif

#		if defined(RENDER_DEPTH)
	const float baseAlpha = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias).w;
	const float diffuseAlpha = input.Fade * baseAlpha;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}

#			ifdef GRASS_OPTIMIZATIONS
	psout.PS.xyz = input.HPosition.zzz;
#			else
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
#			endif
	psout.PS.w = diffuseAlpha;
#		else
	float4 baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
#			if defined(DO_ALPHA_TEST)
	const float diffuseAlpha = input.Color.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0)
		discard;
#			endif

#			ifdef GRASS_OPTIMIZATIONS
	const float lodBrightness = input.LodTier > 1.5 ? SharedData::grassLightingSettings.FarLODBrightness : SharedData::grassLightingSettings.MidLODBrightness;
	baseColor.xyz *= lerp(1.0, lodBrightness, saturate(input.LodTier));
#			endif

	if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (!SharedData::InInterior)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirDetailedShadow = 1.0;

	if (!SharedData::InInterior)
		dirDetailedShadow = shadowColor.x;

#			if defined(SCREEN_SPACE_SHADOWS)
#				ifdef GRASS_OPTIMIZATIONS
	if (!SharedData::InInterior && input.IsFar <= 0.5)
#				else
	if (!SharedData::InInterior)
#				endif
		dirDetailedShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise);
#			endif  // SCREEN_SPACE_SHADOWS

	float3 diffuseColor = dirLightColor * dirDetailedShadow * input.DirLightAngle;

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

#			ifdef GRASS_OPTIMIZATIONS
	float3 ddx = ddx_coarse(viewPosition);
	float3 ddy = ddy_coarse(viewPosition);
#			else
	float3 ddx = ddx_coarse(input.ViewSpacePosition);
	float3 ddy = ddy_coarse(input.ViewSpacePosition);
#			endif
	float3 normalVS = -normalize(cross(ddx, ddy));
	float3 normal = normalize(FrameBuffer::ViewToWorld(normalVS, false));

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	vertexColor /= max(vertexAO, EPSILON_DIVISION);

#			if defined(SKYLIGHTING)
	float3 positionMSSkylight = input.WorldPosition.xyz;
	sh2 skylightingSH = Skylighting::Sample(positionMSSkylight, normal
#				if defined(SKYLIGHTING_SHADOW_VIS)
		,
		skylightingShadowVisibility
#				endif
	);
	float skylightingDiffuse = Skylighting::GetSkylightingDiffuse(skylightingSH, positionMSSkylight, normal, vertexAO);
#			endif  // SKYLIGHTING

	float3 directionalAmbientColor = Color::Ambient(max(0, AmbientColor.xyz));

#			if defined(IBL)
	if (SharedData::iblSettings.EnableIBL)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
#			endif

	float3 albedo = baseColor.xyz * vertexColor;

	diffuseColor += directionalAmbientColor;

	diffuseColor *= albedo;
	directionalAmbientColor *= albedo;

#			if defined(SKYLIGHTING)
	Skylighting::ApplySkylighting(diffuseColor, directionalAmbientColor, albedo, skylightingDiffuse);
#			endif

	psout.Diffuse.xyz = FogNearColor.w * diffuseColor;

	psout.Diffuse.w = 1;

	psout.MotionVectors = MotionBlur::GetSSMotionVector(float4(input.WorldPosition, 1), float4(input.PreviousWorldPosition, 1));
	psout.Normal.xy = GBuffer::EncodeNormal(normalVS);
	psout.Normal.zw = 0;

	psout.Albedo = float4(albedo, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 0);
#		endif

	return psout;
}
#	endif

#endif  // PSHADER
