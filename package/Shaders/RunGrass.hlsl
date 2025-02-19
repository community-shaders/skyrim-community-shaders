#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#ifdef GRASS_LIGHTING
#	define GRASS
#endif  // GRASS_LIGHTING

struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 TexCoord : TEXCOORD0;
	float4 Normal : NORMAL0;
	float4 Color : COLOR0;
	float4 InstanceData1 : TEXCOORD4;
	float4 InstanceData2 : TEXCOORD5;
	float4 InstanceData3 : TEXCOORD6;
	float4 InstanceData4 : TEXCOORD7;
#ifdef VR
	uint InstanceID : SV_INSTANCEID;
#endif  // VR
};

#ifdef GRASS_LIGHTING
struct VS_OUTPUT
{
	float4 HPosition : SV_POSITION0;
	float4 VertexColor : COLOR0;
	float3 TexCoord : TEXCOORD0;
	float3 ViewSpacePosition :
#	if !defined(VR)
		TEXCOORD1;
#	else
		TEXCOORD2;
#	endif
#	if defined(RENDER_DEPTH)
	float2 Depth :
#		if !defined(VR)
		TEXCOORD2;
#		else
		TEXCOORD3;
#		endif
#	endif  // RENDER_DEPTH
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
	float4 VertexNormal : POSITION4;
#	ifdef VR
	float ClipDistance : SV_ClipDistance0;
	float CullDistance : SV_CullDistance0;
#	endif  // VR
};
#else
struct VS_OUTPUT
{
	float4 HPosition : SV_POSITION0;
	float4 DiffuseColor : COLOR0;
	float3 TexCoord : TEXCOORD0;
	float4 AmbientColor : TEXCOORD1;
	float3 ViewSpacePosition : TEXCOORD2;
#	if defined(RENDER_DEPTH)
	float2 Depth : TEXCOORD3;
#	endif  // RENDER_DEPTH
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
#	ifdef VR
	float ClipDistance : SV_ClipDistance0;
	float CullDistance : SV_CullDistance0;
#	endif  // VR
};
#endif

// Constant Buffers (Flat and VR)
cbuffer PerGeometry : register(
#ifdef VSHADER
						  b2
#else
						  b3
#endif
					  )
{
#if !defined(VR)
	row_major float4x4 WorldViewProj[1] : packoffset(c0);
	row_major float4x4 WorldView[1] : packoffset(c4);
	row_major float4x4 World[1] : packoffset(c8);
	row_major float4x4 PreviousWorld[1] : packoffset(c12);
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
#else
	row_major float4x4 WorldViewProj[2] : packoffset(c0);
	row_major float4x4 WorldView[2] : packoffset(c8);
	row_major float4x4 World[2] : packoffset(c16);
	row_major float4x4 PreviousWorld[2] : packoffset(c24);
	float4 FogNearColor : packoffset(c32);
	float3 WindVector : packoffset(c33);
	float WindTimer : packoffset(c33.w);
	float3 DirLightDirection : packoffset(c34);
	float PreviousWindTimer : packoffset(c34.w);
	float3 DirLightColor : packoffset(c35);
	float AlphaParam1 : packoffset(c35.w);
	float3 AmbientColor : packoffset(c36);
	float AlphaParam2 : packoffset(c36.w);
	float3 ScaleMask : packoffset(c37);
	float ShadowClampValue : packoffset(c37.w);
#endif  // !VR
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

#	ifdef GRASS_LIGHTING
float4 GetMSPosition(VS_INPUT input, float windTimer, float3x3 world3x3)
#	else
float4 GetMSPosition(VS_INPUT input, float windTimer)
#	endif
{
	float windAngle = 0.4 * ((input.InstanceData1.x + input.InstanceData1.y) * -0.0078125 + windTimer);
	float windAngleSin, windAngleCos;
	sincos(windAngle, windAngleSin, windAngleCos);

	float windTmp3 = 0.2 * cos(Math::PI * windAngleCos);
	float windTmp1 = sin(Math::PI * windAngleSin);
	float windTmp2 = sin(Math::TAU * windAngleSin);
	float windPower = WindVector.z * (((windTmp1 + windTmp2) * 0.3 + windTmp3) *
										 (0.5 * (input.Color.w * input.Color.w)));

	float3 inputPosition = input.Position.xyz * (input.InstanceData4.yyy * ScaleMask.xyz + float3(1, 1, 1));
	float3 windVector = float3(WindVector.xy, 0);

#	ifdef GRASS_LIGHTING
	float3 InstanceData4 = mul(world3x3, inputPosition);
	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + (windVector * windPower + InstanceData4);
#	else
	float3 instancePosition;
	instancePosition.z = dot(
		float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w), inputPosition);
	instancePosition.x = dot(input.InstanceData2.xyz, inputPosition);
	instancePosition.y = dot(input.InstanceData3.xyz, inputPosition);

	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + (windVector * windPower + instancePosition);
#	endif
	msPosition.w = 1;

	return msPosition;
}

#	ifdef GRASS_LIGHTING
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	uint eyeIndex = Stereo::GetEyeIndexVS(
#		if defined(VR)
		input.InstanceID
#		endif  // VR
	);
	float3x3 world3x3 = float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));

	float4 msPosition = GetMSPosition(input, WindTimer, world3x3);

#		ifdef GRASS_COLLISION
	float3 displacement = GrassCollision::GetDisplacedPosition(msPosition.xyz, input.Color.w, eyeIndex);
	msPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	float4 projSpacePosition = mul(WorldViewProj[eyeIndex], msPosition);
#		if !defined(VR)
	vsout.HPosition = projSpacePosition;
#		endif  // !VR

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);

#		if defined(VR)
	float distanceFade = 1 - saturate((length(mul(World[0], msPosition).xyz) - AlphaParam1) / AlphaParam2);
#		else
	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);
#		endif

	// Note: input.Color.w is used for wind speed
	vsout.VertexColor.xyz = input.Color.xyz * input.InstanceData1.www;
	vsout.VertexColor.w = distanceFade * perInstanceFade;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.ViewSpacePosition = mul(WorldView[eyeIndex], msPosition).xyz;
	vsout.WorldPosition = mul(World[eyeIndex], msPosition);

	float4 previousMsPosition = GetMSPosition(input, PreviousWindTimer, world3x3);

#		ifdef GRASS_COLLISION
	previousMsPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], previousMsPosition);
#		if defined(VR)
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(projSpacePosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#		endif  // !VR

	// Vertex normal needs to be transformed to world-space for lighting calculations.
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
	vsout.VertexNormal.w = input.Color.w;

	return vsout;
}
#	else
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	uint eyeIndex = Stereo::GetEyeIndexVS(
#		if defined(VR)
		input.InstanceID
#		endif  // VR
	);

	float4 msPosition = GetMSPosition(input, WindTimer);

#		ifdef GRASS_COLLISION
	float3 displacement = GrassCollision::GetDisplacedPosition(msPosition.xyz, input.Color.w, 0);
	msPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	float4 projSpacePosition = mul(WorldViewProj[eyeIndex], msPosition);
#		if !defined(VR)
	vsout.HPosition = projSpacePosition;
#		endif  // !VR	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	float dirLightAngle = dot(DirLightDirection.xyz, instanceNormal);
	float3 diffuseMultiplier = input.InstanceData1.www * input.Color.xyz;

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);

#		if defined(VR)
	float distanceFade = 1 - saturate((length(mul(World[0], msPosition).xyz) - AlphaParam1) / AlphaParam2);
#		else
	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);
#		endif

	vsout.DiffuseColor.xyz = diffuseMultiplier;
	vsout.DiffuseColor.w = distanceFade * perInstanceFade;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.AmbientColor.xyz = input.InstanceData1.www * (AmbientColor.xyz * input.Color.xyz);
	vsout.AmbientColor.w = ShadowClampValue;

	vsout.ViewSpacePosition = mul(WorldView[eyeIndex], msPosition).xyz;
	vsout.WorldPosition = mul(World[eyeIndex], msPosition);

	float4 previousMsPosition = GetMSPosition(input, PreviousWindTimer);
#		if defined(VR)
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(projSpacePosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#		endif  // !VR

#		ifdef GRASS_COLLISION
	previousMsPosition.xyz += displacement;
#		endif  // GRASS_COLLISION

	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], previousMsPosition);

	return vsout;
}

#	endif

#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#ifdef GRASS_LIGHTING
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS : SV_Target0;
#	else
	float4 Diffuse : SV_Target0;
	float2 MotionVectors : SV_Target1;
	float4 NormalGlossiness : SV_Target2;
	float4 Albedo : SV_Target3;
	float4 Specular : SV_Target4;
#		if defined(TRUE_PBR)
	float4 Reflectance : SV_Target5;
#		endif  // TRUE_PBR
	float4 Masks : SV_Target6;
#		if defined(TRUE_PBR)
	float4 Parameters : SV_Target7;
#		endif  // TRUE_PBR
#	endif      // RENDER_DEPTH
};
#else
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS : SV_Target0;
#	else
	float4 Diffuse : SV_Target0;
	float2 MotionVectors : SV_Target1;
	float4 Normal : SV_Target2;
	float4 Albedo : SV_Target3;
	float4 Masks : SV_Target6;
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

#	if !defined(VR)
cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif  // !VR

#	if defined(SCREEN_SPACE_SHADOWS)
#		include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#	endif

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	define SampColorSampler SampBaseSampler

#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(TERRAIN_SHADOWS)
#		include "TerrainShadows/TerrainShadows.hlsli"
#	endif

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	if defined(WATER_LIGHTING)
#		include "WaterLighting/WaterCaustics.hlsli"
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

PS_OUTPUT main(PS_INPUT input, bool frontFace
			   : SV_IsFrontFace)
{
	PS_OUTPUT psout;

#		if !defined(TRUE_PBR)
	float x;
	float y;
	TexBaseSampler.GetDimensions(x, y);

	bool complex = x != y;
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

#		if defined(RENDER_DEPTH)
	float diffuseAlpha = input.VertexColor.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else
#			if !defined(TRUE_PBR)
	float4 specColor = complex ? TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, 0.5 + input.TexCoord.y * 0.5), SharedData::MipBias) : 1;
#			else
	float4 specColor = TexNormalSampler.SampleBias(SampNormalSampler, input.TexCoord.xy, SharedData::MipBias);
#			endif

	uint eyeIndex = Stereo::GetEyeIndexPS(input.HPosition, VPOSOffset);
	psout.MotionVectors = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float3 normal = normalize(input.VertexNormal.xyz);

	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	// Swaps direction of the backfaces otherwise they seem to get lit from the wrong direction.
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

	float3 dirLightColor = SharedData::DirLightColor.xyz;
	float3 dirLightColorMultiplier = 1;

	float dirLightAngle = dot(normal, SharedData::DirLightDirection.xyz);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float dirShadow = !SharedData::InInterior ? shadowColor.x : 1.0;
	float dirDetailShadow = 1.0;

	if (dirShadow > 0.0 && !SharedData::InInterior) {
		if (dirLightAngle > 0.0) {
#			if defined(SCREEN_SPACE_SHADOWS)
			dirDetailShadow = ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise, eyeIndex);
#			endif  // SCREEN_SPACE_SHADOWS
		}

		if (dirShadow != 0.0)
			dirShadow *= ShadowSampling::GetWorldShadow(input.WorldPosition, FrameBuffer::CameraPosAdjust[eyeIndex], eyeIndex);

#			if defined(WATER_LIGHTING)
		if (dirShadow > 0.0) {
			float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
			dirShadow *= WaterLighting::ComputeCaustics(waterData, input.WorldPosition.xyz, eyeIndex);
		}
#			endif
	}

	float3 diffuseColor = 0;
	float3 specularColor = 0;

	float3 lightsDiffuseColor = 0;
	float3 lightsSpecularColor = 0;

#			if defined(TRUE_PBR)
	{
		PBR::LightProperties lightProperties = PBR::InitLightProperties(SharedData::DirLightColor.xyz, dirLightColorMultiplier * dirShadow, 1);
		float3 dirDiffuseColor, coatDirDiffuseColor, dirTransmissionColor, dirSpecularColor;
		PBR::GetDirectLightInput(dirDiffuseColor, coatDirDiffuseColor, dirTransmissionColor, dirSpecularColor, normal, normal, viewDirection, viewDirection, DirLightDirection, DirLightDirection, lightProperties, pbrSurfaceProperties, tbn, input.TexCoord.xy);
		lightsDiffuseColor += dirDiffuseColor;
		transmissionColor += dirTransmissionColor;
		specularColorPBR += dirSpecularColor;
	}
#			else
	dirLightColor *= dirLightColorMultiplier;
	dirLightColor *= dirShadow;

	lightsDiffuseColor += dirLightColor * saturate(dirLightAngle) * dirDetailShadow;

	float3 albedo = max(0, baseColor.xyz * input.VertexColor.xyz);

	float3 subsurfaceColor = lerp(Color::RGBToLuminance(albedo.xyz), albedo.xyz, 2.0) * saturate(input.VertexNormal.w * 10.0);

	float3 sss = dirLightColor * saturate(-dirLightAngle);

	if (complex)
		lightsSpecularColor += GrassLighting::GetLightSpecularInput(DirLightDirection, viewDirection, normal, dirLightColor, SharedData::grassLightingSettings.Glossiness);
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

				float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
				float3 lightColor = light.color.xyz * intensityMultiplier;

				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				float3 normalizedLightDirection = normalize(lightDirection);

				float lightAngle = dot(normal, normalizedLightDirection);

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

				float3 lightDiffuseColor = lightColor * saturate(dirLightAngle);
				sss += lightColor * saturate(-lightAngle);

				lightsDiffuseColor += lightDiffuseColor;

				if (complex)
					lightsSpecularColor += GrassLighting::GetLightSpecularInput(normalizedLightDirection, viewDirection, normal, lightColor, SharedData::grassLightingSettings.Glossiness) * intensityMultiplier;
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
	specularColor.xyz = Color::LinearToGamma(specularColor.xyz);
	diffuseColor.xyz = Color::LinearToGamma(diffuseColor.xyz);
#			else

#				if !defined(SSGI)
	float3 directionalAmbientColor = mul(SharedData::DirectionalAmbient, float4(normal, 1.0));

#					if defined(SKYLIGHTING)
#						if defined(VR)
	float3 positionMSSkylight = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#						else
	float3 positionMSSkylight = input.WorldPosition.xyz;
#						endif

	sh2 skylightingSH = Skylighting::sample(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, Skylighting::stbn_vec3_2Dx1D_128x128x64, input.HPosition.xy, positionMSSkylight, normal);
	float skylighting = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(float3(normal.xy, normal.z * 0.5 + 0.5))) / Math::PI;
	skylighting = lerp(1.0, skylighting, Skylighting::getFadeOutFactor(input.WorldPosition));
	skylighting = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylighting);

	directionalAmbientColor = Color::GammaToLinear(directionalAmbientColor);
	directionalAmbientColor *= skylighting;
	directionalAmbientColor = Color::LinearToGamma(directionalAmbientColor);
#					endif  // SKYLIGHTING

	diffuseColor += directionalAmbientColor;
#				endif      // !SSGI

	diffuseColor *= albedo;
	diffuseColor += max(0, sss * subsurfaceColor * SharedData::grassLightingSettings.SubsurfaceScatteringAmount);

	specularColor += lightsSpecularColor;
	specularColor *= specColor.w * SharedData::grassLightingSettings.SpecularStrength;
	specularColor = Color::GammaToLinear(specularColor);
#			endif

#			if defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 0) {
			diffuseColor.xyz = LightLimitFix::TurboColormap(0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			diffuseColor.xyz = LightLimitFix::TurboColormap(0);
		} else {
			diffuseColor.xyz = LightLimitFix::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
		}
	} else {
		psout.Diffuse = float4(diffuseColor, 1);
	}
#			else
	psout.Diffuse.xyz = diffuseColor;
#			endif

	float3 normalVS = normalize(FrameBuffer::WorldToView(normal, false, eyeIndex));
#			if defined(TRUE_PBR)
	psout.Albedo = float4(Color::LinearToGamma(indirectDiffuseLobeWeight), 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), 1 - pbrSurfaceProperties.Roughness, 1);
	psout.Reflectance = float4(indirectSpecularLobeWeight, 1);
	psout.Parameters = float4(0, 0, 1, 1);
#			else
	psout.Albedo = float4(albedo, 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), specColor.w, 1);
#			endif

	psout.Specular = float4(specularColor, 1);
	psout.Masks = float4(0, 0, 0, 0);
#		endif
	return psout;
}
#	else
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);

#		if defined(RENDER_DEPTH)
	float diffuseAlpha = input.DiffuseColor.w * baseColor.w;

	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else

	uint eyeIndex = Stereo::GetEyeIndexPS(input.HPosition, VPOSOffset);

	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float dirShadow = !SharedData::InInterior ? shadowColor.x : 1.0;
	float dirDetailShadow = 1.0;

	if (dirShadow > 0.0 && !SharedData::InInterior) {
#			if defined(SCREEN_SPACE_SHADOWS)
		dirDetailShadow = ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise, eyeIndex);
#			endif  // SCREEN_SPACE_SHADOWS

		if (dirShadow != 0.0)
			dirShadow *= ShadowSampling::GetWorldShadow(input.WorldPosition, FrameBuffer::CameraPosAdjust[eyeIndex], eyeIndex);

#			if defined(WATER_LIGHTING)
		if (dirShadow > 0.0) {
			float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
			dirShadow *= WaterLighting::ComputeCaustics(waterData, input.WorldPosition.xyz, eyeIndex);
		}
#			endif
	}

	float3 diffuseColor = SharedData::DirLightColor.xyz * dirShadow * lerp(dirDetailShadow, 1.0, 0.5) * 0.5;

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

				float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
				float3 lightColor = light.color.xyz * intensityMultiplier;

				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				lightColor *= lightShadow;

				diffuseColor += lightColor * 0.5;
			}
		}
	}
#			endif  // LIGHT_LIMIT_FIX

	float3 ddx = ddx_coarse(input.WorldPosition);
	float3 ddy = ddy_coarse(input.WorldPosition);
	float3 normal = normalize(cross(ddx, ddy));

	normal = float3(normal.xy, normal.z * 0.5 + 0.5);

#			if !defined(SSGI)
	float3 directionalAmbientColor = mul(SharedData::DirectionalAmbient, float4(normal, 1.0));

#				if defined(SKYLIGHTING)
#					if defined(VR)
	float3 positionMSSkylight = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#					else
	float3 positionMSSkylight = input.WorldPosition.xyz;
#					endif

	sh2 skylightingSH = Skylighting::sample(SharedData::skylightingSettings, Skylighting::SkylightingProbeArray, Skylighting::stbn_vec3_2Dx1D_128x128x64, input.HPosition.xy, positionMSSkylight, normal);
	float skylighting = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(float3(normal.xy, normal.z * 0.5 + 0.5))) / Math::PI;
	skylighting = lerp(1.0, skylighting, Skylighting::getFadeOutFactor(input.WorldPosition));
	skylighting = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylighting);

	directionalAmbientColor = Color::GammaToLinear(directionalAmbientColor);
	directionalAmbientColor *= skylighting;
	directionalAmbientColor = Color::LinearToGamma(directionalAmbientColor);
#				endif  // SKYLIGHTING

	diffuseColor += directionalAmbientColor;
#			endif      // !SSGI

	float3 albedo = baseColor.xyz * input.DiffuseColor.xyz;
	psout.Diffuse.xyz = diffuseColor * albedo;

	psout.Diffuse.w = 1;

	psout.MotionVectors = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);
	psout.Normal.xy = GBuffer::EncodeNormal(FrameBuffer::WorldToView(normal, false, eyeIndex));
	psout.Normal.zw = 0;

	psout.Albedo = float4(albedo, 1);
	psout.Masks = float4(0, 0, 0, 0);
#		endif

	return psout;
}
#	endif

#endif  // PSHADER
