
#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "Common/VR.hlsli"

Texture2D<float4> MainInputTexture : register(t0);
Texture2D<float3> SpecularTexture : register(t1);

#if defined(SSGI) || defined(DYNAMIC_CUBEMAPS) || defined(DEBUG)
Texture2D<float4> NormalRoughnessTexture : register(t2);
#endif

#if defined(SSGI) || defined(DYNAMIC_CUBEMAPS)
Texture2D<float> DepthTexture : register(t3);
#endif

#if defined(SSGI) || defined(DEBUG)
Texture2D<float3> AlbedoTexture : register(t4);
#endif

#if defined(SSGI)
Texture2D<float3> MasksTexture : register(t5);
#endif

#if defined(DYNAMIC_CUBEMAPS)
Texture2D<float3> ReflectanceTexture : register(t6);
TextureCube<float3> EnvTexture : register(t7);
TextureCube<float3> EnvReflectionsTexture : register(t8);

SamplerState LinearSampler : register(s0);
#endif

#if defined(SKYLIGHTING)
#	define SKYLIGHTING_PROBE_REGISTER t9
#	include "Skylighting/Skylighting.hlsli"
#endif

#if defined(SSRT)
Texture2D<float4> SsrtTexture : register(t10);

void SampleSSRT(uint2 pixCoord, float3 normalWS, out float ao, out float3 il)
{
	float4 ssrt = SsrtTexture[pixCoord];
	ao = ssrt.a;  // SSRT3: 1 = no occlusion, 0 = full occlusion
	il = ssrt.rgb;
}
#endif

#if defined(IBL)
#	if !defined(DYNAMIC_CUBEMAPS)
#		undef IBL
#	else
#		define IBL_DEFERRED
#		include "IBL/IBL.hlsli"
#	endif
#endif

struct PS_INPUT
{
	float4 Position: SV_Position;
	float2 TexCoord: TEXCOORD0;
};

struct PS_OUTPUT
{
	float4 Main: SV_Target0;
	float4 NormalRoughness: SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
	uint2 pixCoord = uint2(input.Position.xy);

	float2 uv = float2(pixCoord + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float3 diffuseColor = MainInputTexture[pixCoord].xyz;
	float3 specularColor = SpecularTexture[pixCoord];
	float3 linDiffuseColor = Color::IrradianceToLinear(diffuseColor);

#if defined(SSGI) || defined(DYNAMIC_CUBEMAPS)
	float3 normalGlossiness = NormalRoughnessTexture[pixCoord].xyz;
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);
	float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

	float depth = DepthTexture[pixCoord];
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;
#endif

#if defined(DYNAMIC_CUBEMAPS)
	float glossiness = normalGlossiness.z;
#endif

#if defined(SSRT)

	float ssrtAo;
	float3 ssrtIl;
	SampleSSRT(dispatchID.xy, normalWS, ssrtAo, ssrtIl);

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normalWS)));
	directionalAmbientColor *= albedo;

	directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
	directionalAmbientColor.x = MasksTexture[dispatchID.xy].z;
	directionalAmbientColor = Color::YCoCgToRGB(directionalAmbientColor);
	directionalAmbientColor = max(0, directionalAmbientColor);

	float maxScale = 1.0;
	if (directionalAmbientColor.x > 0.0)
		maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
	if (directionalAmbientColor.y > 0.0)
		maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
	if (directionalAmbientColor.z > 0.0)
		maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
	directionalAmbientColor *= maxScale;

	diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);

	linDiffuseColor = Color::IrradianceToLinear(diffuseColor);

	float3 linAlbedo = Color::IrradianceToLinear(albedo / Color::PBRLightingScale);

	float3 multiBounceSsrtAo = MultiBounceAO(linAlbedo, ssrtAo);

	linDiffuseColor *= sqrt(multiBounceSsrtAo);

#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		float3 vanillaDALC = Color::Ambient(max(0, SharedData::GetAmbient(normalWS)));

	diffuseColor += Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColor) * multiBounceSsrtAo);

		directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
		directionalAmbientColor.x = MasksTexture[pixCoord].z;
		directionalAmbientColor = Color::YCoCgToRGB(directionalAmbientColor);
		directionalAmbientColor = max(0, directionalAmbientColor);
	} else
#	endif
	{
		directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normalWS)));
		directionalAmbientColor *= albedo;

	linDiffuseColor += ssrtIl * linAlbedo;
#endif

	float3 color = linDiffuseColor + specularColor;

#if defined(DYNAMIC_CUBEMAPS)

	float3 reflectance = ReflectanceTexture[pixCoord];

	if (any(reflectance > 0.0)) {
		float3 V = -normalize(positionWS.xyz);
		float3 R = reflect(-V, normalWS);

		float roughness = 1.0 - glossiness;
		float level = roughness * 7.0;

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normalWS, V, roughness);

		float3 finalIrradiance = 0;

		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(max(0, SharedData::GetAmbient(R)))) * Color::ReflectionNormalisationScale;

#	if defined(SKYLIGHTING)
#		if defined(VR)
		float3 positionMS = positionWS.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#		else
		float3 positionMS = positionWS.xyz;
#		endif

		sh2 skylightingSH = Skylighting::Sample(positionMS.xyz, R);
		float skylightingSpecular = Skylighting::EvaluateSpecular(skylightingSH, specularLobe);
#	endif

#	if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			float3 envSample = EnvTexture.SampleLevel(LinearSampler, R, level);
			float3 fullSample = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
			float3 envSpecular, skySpecular;

			if (SharedData::iblSettings.DALCMode >= 2) {
				// Mode 2/3: DALC-normalized env scaled by DALCAmount + sky overlay
				float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
				envSpecular = Color::IrradianceToLinear((envSample / max(envLum, 0.001)) * directionalAmbientColorSpecular) * SharedData::iblSettings.DALCAmount;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#		if defined(SKYLIGHTING)
				envSpecular *= (SharedData::iblSettings.DALCMode == 3) ? skylightingSpecular : 1.0;
				skySpecular *= skylightingSpecular;
#		elif defined(INTERIOR)
				skySpecular = 0;
#		endif
			} else {
				// Mode 0/1: IBL ratio-based
				float3 ratio = ImageBasedLighting::GetIBLRatio();
				envSpecular = Color::IrradianceToLinear(envSample * ratio) * SharedData::iblSettings.EnvIBLScale;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#		if defined(SKYLIGHTING)
				skySpecular *= skylightingSpecular;
#		elif defined(INTERIOR)
				skySpecular = 0;
#		endif
			}

			finalIrradiance = envSpecular + skySpecular;
		} else
#	endif
		{
			// Fallback without IBL: normalize-by-luminance with DALC
#	if defined(INTERIOR)
			float3 specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#	elif defined(SKYLIGHTING)
			float3 specularIrradianceReflections = 0.0;
			if (skylightingSpecular > 0.0) {
				specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
				float lum = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, R, 15));
				specularIrradianceReflections = (specularIrradianceReflections / max(lum, 0.001)) * directionalAmbientColorSpecular;
				specularIrradianceReflections = Color::IrradianceToLinear(specularIrradianceReflections);
			}
			float3 specularIrradiance = 0.0;
			if (skylightingSpecular < 1.0) {
				specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level);
				float lum = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
				float dalcScaled = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColorSpecular) * skylightingSpecular);
				specularIrradiance = (specularIrradiance / max(lum, 0.001)) * dalcScaled;
				specularIrradiance = Color::IrradianceToLinear(specularIrradiance);
			}
			finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#	else
			float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#	endif
		}

#	if defined(SSRT)
		finalIrradiance *= ssrtAo;
#	endif

		color += reflectance * finalIrradiance;
	}

#endif

	color = Color::IrradianceToGamma(color);

#if defined(DEBUG)

#	if !defined(SSGI) && !defined(DYNAMIC_CUBEMAPS)
	float3 normalGlossiness = NormalRoughnessTexture[pixCoord];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);
#	endif

#	if !defined(SSGI)
	float3 albedo = AlbedoTexture[pixCoord];
#	endif

#	if !defined(DYNAMIC_CUBEMAPS)
	float glossiness = normalGlossiness.z;
#	endif

#	if defined(VR)
	uv.x += (eyeIndex ? 0.1 : -0.1);
#	endif

	if (uv.x < 0.5 && uv.y < 0.5) {
		color = color;
	} else if (uv.x < 0.5) {
		color = albedo;
	} else if (uv.y < 0.5) {
		color = normalVS;
	} else {
		color = glossiness;
	}

#endif

	PS_OUTPUT output;
	output.Main = float4(color, 1.0);
	output.NormalRoughness = 0;
	return output;
}
