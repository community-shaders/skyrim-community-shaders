
#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

Texture2D<float3> SpecularTexture : register(t0);
Texture2D<unorm float3> AlbedoTexture : register(t1);
Texture2D<unorm float3> NormalRoughnessTexture : register(t2);
Texture2D<float3> MasksTexture : register(t3);
Texture2D<unorm float> Masks2Texture : register(t14);

RWTexture2D<float4> MainRW : register(u0);
RWTexture2D<float4> NormalTAAMaskSpecularMaskRW : register(u1);
RWTexture2D<float2> MotionVectorsRW : register(u2);

// 24/32-bit depth: TerrainBlending ON -> R32_FLOAT (no unorm),
// OFF -> R24_UNORM_X8_TYPELESS game depth (unorm).
#if defined(TERRAIN_BLENDING)
Texture2D<float> DepthTexture : register(t4);
#else
Texture2D<unorm float> DepthTexture : register(t4);
#endif

#if defined(DYNAMIC_CUBEMAPS)
Texture2D<float3> ReflectanceTexture : register(t5);
TextureCube<float3> EnvTexture : register(t6);
TextureCube<float3> EnvReflectionsTexture : register(t7);

SamplerState LinearSampler : register(s0);
#endif

#if defined(SKYLIGHTING)
#	define SKYLIGHTING_PROBE_REGISTER t8
#	include "Skylighting/Skylighting.hlsli"
#endif

#if defined(SSGI) || defined(SSR)
#	include "NRD/NRDReblurSH.hlsli"
#endif

#if defined(DEBUG_VIEW)
cbuffer DeferredDebugCB : register(b13)
{
	uint DebugView;
	float3 DebugPad;
};
#endif

float GetSpecularOcclusionFromAmbientOcclusion(float NdotV, float ao, float roughness)
{
	return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

#if defined(SSGI)
Texture2D<float4> SsgiTexture : register(t9);
#	if defined(SSGI_SH)
Texture2D<float4> SsgiSH1Texture : register(t10);
#	endif

float SampleSSGIAO(uint2 pixCoord)
{
#	if defined(SSGI_SH)
	NRD_SG sg = REBLUR_BackEnd_UnpackSh(SsgiTexture[pixCoord], SsgiSH1Texture[pixCoord]);
	return saturate(sg.normHitDist);
#	else
	float4 data = SsgiTexture[pixCoord];
	float normHitDist;
	float3 radiance;
	REBLUR_BackEnd_UnpackRadianceAndNormHitDist(data, radiance, normHitDist);
	return saturate(normHitDist);
#	endif
}
#endif

#if defined(SSR)
Texture2D<float4> SsrTexture : register(t13);

void SampleSSRTracedSpecular(uint2 pixCoord, out float3 specularRadiance, out float normHitDist)
{
	float4 data = SsrTexture[pixCoord];
	REBLUR_BackEnd_UnpackRadianceAndNormHitDist(data, specularRadiance, normHitDist);
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

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside screen bounds
	if (any(dispatchID.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res

	float3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float3 diffuseColor = MainRW[dispatchID.xy].xyz;
	float3 specularColor = SpecularTexture[dispatchID.xy];
	float3 albedo = AlbedoTexture[dispatchID.xy];

#if defined(DEBUG_VIEW)
	float3 inputDiffuseColor = diffuseColor;
#endif

	float depth = DepthTexture[dispatchID.xy];
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse, positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

	if (depth == 1.0)
		MotionVectorsRW[dispatchID.xy] = MotionBlur::GetSSMotionVector(positionWS, positionWS);  // Apply sky motion vectors

	float glossiness = normalGlossiness.z;

#if defined(DEBUG_VIEW)
	float3 debugV = -normalize(positionWS.xyz);
	float3 debugCubemapIrradiance = 0;
#endif

	float3 linDiffuseColor = Color::IrradianceToLinear(diffuseColor);
	float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse, float4(normalVS, 0)).xyz);

	float ssgiAo = 1.0;

#if defined(SSGI)
	if (depth < 1.0 - 1e-6 && SharedData::ssgiSettings.Enabled != 0) {
		float3 multiBounceSSGIAo = 1.0;
		ssgiAo = SampleSSGIAO(dispatchID.xy);
		if (SharedData::ssgiSettings.EnableIL == 0)
			ssgiAo = pow(max(ssgiAo, EPSILON_DIVISION), SharedData::ssgiSettings.AOPower);
		float3 linAlbedo = Color::IrradianceToLinear(albedo / Color::PBRLightingScale);
		float vertexAO = 1.0 - Masks2Texture[dispatchID.xy].x;
		ssgiAo = saturate(ssgiAo / max(vertexAO, EPSILON_DIVISION));
		multiBounceSSGIAo = MultiBounceAO(linAlbedo, ssgiAo);

		if (SharedData::ssgiSettings.EnableIL != 0) {
			// IL replaces directional ambient in the deferred main view, so there is no ambient term to reconstruct here.
			linDiffuseColor *= sqrt(multiBounceSSGIAo);
		} else {
			float3 directionalAmbientColor = 0;

#	if defined(IBL)
			if (SharedData::iblSettings.EnableIBL) {
				float3 vanillaDALC = Color::Ambient(max(0, SharedData::GetAmbient(normalWS)));

#		if defined(SKYLIGHTING)
				float3 positionMS = positionWS.xyz;
				sh2 skylightingSH = Skylighting::Sample(positionMS.xyz, normalWS);
				float skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, normalWS);
				directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(vanillaDALC, -normalWS, skylightingDiffuse, positionWS.xyz) * albedo;
#		else
				directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(vanillaDALC, -normalWS, positionWS.xyz) * albedo;
#		endif

				directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
				directionalAmbientColor.x = MasksTexture[dispatchID.xy].z;
				directionalAmbientColor = Color::YCoCgToRGB(directionalAmbientColor);
				directionalAmbientColor = max(0, directionalAmbientColor);
			} else
#	endif
			{
				directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normalWS)));
				directionalAmbientColor *= albedo;

				directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
				directionalAmbientColor.x = MasksTexture[dispatchID.xy].z;
				directionalAmbientColor = Color::YCoCgToRGB(directionalAmbientColor);
				directionalAmbientColor = max(0, directionalAmbientColor);
			}

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
			linDiffuseColor *= sqrt(multiBounceSSGIAo);
			diffuseColor = Color::IrradianceToGamma(linDiffuseColor);

			float3 linDirectionalAmbientColor = Color::IrradianceToLinear(directionalAmbientColor);
			directionalAmbientColor = Color::IrradianceToGamma(linDirectionalAmbientColor * multiBounceSSGIAo);
			diffuseColor += directionalAmbientColor;
			linDiffuseColor = Color::IrradianceToLinear(diffuseColor);
		}
	}
#endif

	float3 color = linDiffuseColor + specularColor;

#if defined(DYNAMIC_CUBEMAPS)

	float3 reflectance = ReflectanceTexture[dispatchID.xy];

	if (any(reflectance > 0.0)) {
		float3 V = -normalize(positionWS.xyz);
		float3 R = reflect(-V, normalWS);

		float roughness = 1.0 - glossiness;
		float level = roughness * 8.0;

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normalWS, V, roughness);

		float3 finalIrradiance = 0;
		{
			float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(max(0, SharedData::GetAmbient(R)))) * Color::ReflectionNormalisationScale;

#	if defined(SKYLIGHTING)
			float3 positionMS = positionWS.xyz;

			sh2 skylightingSH = Skylighting::Sample(positionMS.xyz, R);
			float skylightingSpecular = Skylighting::EvaluateSpecular(skylightingSH, specularLobe);
#	endif

#	if defined(IBL)
			if (SharedData::iblSettings.EnableIBL) {
				float3 envSample = EnvTexture.SampleLevel(LinearSampler, R, level);
				float3 fullSample = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
				float3 envSpecular, skySpecular;

				if (SharedData::iblSettings.DALCMode >= 2) {
					float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
					envSpecular = Color::IrradianceToLinear((envSample / max(envLum, 0.001)) * directionalAmbientColorSpecular) * SharedData::iblSettings.DALCAmount;
					skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
				} else {
					float3 ratio = ImageBasedLighting::GetIBLRatio();
					envSpecular = Color::IrradianceToLinear(envSample * ratio) * SharedData::iblSettings.EnvIBLScale;
					skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
				}
#		if defined(SKYLIGHTING)
				skySpecular *= skylightingSpecular;
#		elif defined(INTERIOR)
				skySpecular = 0;
#		endif
				if (SharedData::InInterior) {
					skySpecular = 0;
				}

				finalIrradiance = envSpecular + skySpecular;
			} else
#	endif
			{
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

#	if defined(DEBUG_VIEW)
			debugCubemapIrradiance = finalIrradiance;
#	endif

#	if defined(SSR)
			if (SharedData::ssrSettings.Enabled != 0) {
				float3 tracedSpecular;
				float specNormHitDist;
				SampleSSRTracedSpecular(dispatchID.xy, tracedSpecular, specNormHitDist);

				float tracedAvailability = saturate(specNormHitDist / NRD_EPS);
				float3 fallbackIrradiance = finalIrradiance * SharedData::ssrSettings.SpecCubemapMult;
				finalIrradiance = lerp(fallbackIrradiance, tracedSpecular, tracedAvailability);
			}
#	endif

#	if defined(SSGI)
			if (SharedData::ssgiSettings.Enabled != 0) {
				float ssgiSpecAo = GetSpecularOcclusionFromAmbientOcclusion(saturate(dot(normalWS, V)), ssgiAo, roughness);
				finalIrradiance *= ssgiSpecAo;
			}
#	endif
		}

		color += reflectance * finalIrradiance;
	}

#endif

	color = Color::IrradianceToGamma(color);

#if defined(DEBUG)

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

#if defined(DEBUG_VIEW)

	if (DebugView != 0) {
		float roughness = 1.0 - glossiness;

		if (DebugView == 1) {
			color = albedo;
		} else if (DebugView == 2) {
#	if defined(DYNAMIC_CUBEMAPS)
			color = ReflectanceTexture[dispatchID.xy];
#	else
			color = 0;
#	endif
		} else if (DebugView == 3) {
			color = normalVS;
		} else if (DebugView == 4) {
			color = roughness;
		} else if (DebugView == 5) {
			color = MasksTexture[dispatchID.xy];
		} else if (DebugView == 6) {
			color = inputDiffuseColor;
		} else if (DebugView == 7) {
			color = Color::IrradianceToGamma(max(specularColor, 0));
		} else if (DebugView == 8) {
#	if defined(SSGI)
#		if defined(SSGI_SH)
			NRD_SG sg = REBLUR_BackEnd_UnpackSh(SsgiTexture[dispatchID.xy], SsgiSH1Texture[dispatchID.xy]);
			color = NRD_SG_ResolveDiffuse(sg, normalWS, debugV, roughness);
#		else
			float normHitDist;
			REBLUR_BackEnd_UnpackRadianceAndNormHitDist(SsgiTexture[dispatchID.xy], color, normHitDist);
#		endif
			color = Color::IrradianceToGamma(max(color, 0));
#	else
			color = 0;
#	endif
		} else if (DebugView == 9) {
#	if defined(SSR)
			float normHitDist;
			REBLUR_BackEnd_UnpackRadianceAndNormHitDist(SsrTexture[dispatchID.xy], color, normHitDist);
			color = Color::IrradianceToGamma(max(color, 0));
#	else
			color = 0;
#	endif
		} else if (DebugView == 10) {
#	if defined(DYNAMIC_CUBEMAPS)
			color = Color::IrradianceToGamma(max(debugCubemapIrradiance, 0));
#	else
			color = 0;
#	endif
		}
	}

#endif

	MainRW[dispatchID.xy] = float4(color, 1.0);
	NormalTAAMaskSpecularMaskRW[dispatchID.xy] = float4(GBuffer::EncodeNormalVanilla(normalVS), 0.0, 0.0);
}
