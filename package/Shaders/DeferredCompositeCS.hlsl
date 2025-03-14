
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "Common/VR.hlsli"

Texture2D<float3> SpecularTexture : register(t0);
Texture2D<unorm float3> AlbedoTexture : register(t1);
Texture2D<unorm float3> NormalRoughnessTexture : register(t2);
Texture2D<unorm float3> MasksTexture : register(t3);

RWTexture2D<float4> MainRW : register(u0);
RWTexture2D<float4> NormalTAAMaskSpecularMaskRW : register(u1);
RWTexture2D<float2> MotionVectorsRW : register(u2);
Texture2D<float> DepthTexture : register(t4);

#if defined(DYNAMIC_CUBEMAPS)
Texture2D<float3> ReflectanceTexture : register(t5);
TextureCube<float3> EnvTexture : register(t6);
TextureCube<float3> EnvReflectionsTexture : register(t7);

SamplerState LinearSampler : register(s0);
#endif

#if defined(SKYLIGHTING)
#	include "Skylighting/Skylighting.hlsli"

Texture3D<sh2> SkylightingProbeArray : register(t8);
Texture2DArray<float3> stbn_vec3_2Dx1D_128x128x64 : register(t9);

#endif

#if defined(SSGI)
Texture2D<float4> SsgiAoTexture : register(t10);
Texture2D<float4> SsgiYTexture : register(t11);
Texture2D<float4> SsgiCoCgTexture : register(t12);
Texture2D<float4> SsgiSpecularTexture : register(t13);

void SampleSSGISpecular(uint2 pixCoord, sh2 lobe, out float ao, out float3 il, in float3 normal, in float3 view)
{
	// https://www.iryoku.com/stare-into-the-future/
	ao = 1 - SsgiAoTexture[pixCoord];
	const float SpecularPow = 8.0;
	float NdotV = dot(normal, view);
	float s = saturate(-0.3 + NdotV * NdotV);
	ao = lerp(pow(ao, SpecularPow), 1.0, s);

	float4 ssgiIlYSh = SsgiYTexture[pixCoord];
	float ssgiIlY = SphericalHarmonics::FuncProductIntegral(ssgiIlYSh, lobe);
	float2 ssgiIlCoCg = SsgiCoCgTexture[pixCoord];
	// specular is a bit too saturated, because CoCg are average over hemisphere
	// we just cheese this bit
	ssgiIlCoCg *= 0.8;

	// pi to compensate for the /pi in specularLobe
	// i don't think there really should be a 1/PI but without it the specular is too strong
	// reflectance being ambient reflectance doesn't help either
	il = max(0, Color::YCoCgToRGB(float3(ssgiIlY, ssgiIlCoCg / Math::PI)));

	// HQ spec
	float4 hq_spec = SsgiSpecularTexture[pixCoord];
	ao *= 1 - hq_spec.a;
	il += hq_spec.rgb;
}
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float3 diffuseColor = MainRW[dispatchID.xy];
	float3 specularColor = SpecularTexture[dispatchID.xy];
	float3 albedo = AlbedoTexture[dispatchID.xy];

	float depth = DepthTexture[dispatchID.xy];
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

	if (depth == 1.0)
		MotionVectorsRW[dispatchID.xy] = MotionBlur::GetSSMotionVector(positionWS, positionWS, eyeIndex);  // Apply sky motion vectors

	float glossiness = normalGlossiness.z;

	float3 color = Color::GammaToLinear(diffuseColor) + specularColor;

#if defined(DYNAMIC_CUBEMAPS)

	float3 reflectance = ReflectanceTexture[dispatchID.xy];

	if (reflectance.x > 0.0 || reflectance.y > 0.0 || reflectance.z > 0.0) {
		float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

		float wetnessMask = MasksTexture[dispatchID.xy].z;

		normalWS = lerp(normalWS, float3(0, 0, 1), wetnessMask);

		float3 V = normalize(positionWS.xyz);
		float3 R = reflect(V, normalWS);

		float roughness = 1.0 - glossiness;
		float level = roughness * 7.0;

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normalWS, -V, roughness);

		float3 finalIrradiance = 0;

#	if defined(INTERIOR)
		float3 specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(LinearSampler, R, level));

		finalIrradiance += specularIrradiance;
#	elif defined(SKYLIGHTING)
#		if defined(VR)
		float3 positionMS = positionWS.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#		else
		float3 positionMS = positionWS.xyz;
#		endif

		sh2 skylighting = Skylighting::sample(SharedData::skylightingSettings, SkylightingProbeArray, stbn_vec3_2Dx1D_128x128x64, dispatchID.xy, positionMS.xyz, R);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		float3 specularIrradiance = 1;

		if (skylightingSpecular < 1.0)
			specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(LinearSampler, R, level));

		float3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0)
			specularIrradianceReflections = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(LinearSampler, R, level));

		finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#	else
		float3 specularIrradianceReflections = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(LinearSampler, R, level));

		finalIrradiance += specularIrradianceReflections;
#	endif

#	if defined(SSGI)
#		if defined(VR)
		float3 uvF = float3((dispatchID.xy + 0.5) * SharedData::BufferDim.zw, DepthTexture[dispatchID.xy]);  // calculate high precision uv of initial eye
		float3 uv2 = Stereo::ConvertStereoUVToOtherEyeStereoUV(uvF, eyeIndex, false);                        // calculate other eye uv
		float3 uv1Mono = Stereo::ConvertFromStereoUV(uvF, eyeIndex);
		float3 uv2Mono = Stereo::ConvertFromStereoUV(uv2, (1 - eyeIndex));
		uint2 pixCoord2 = (uint2)(uv2.xy / SharedData::BufferDim.zw - 0.5);
#		endif

		float ssgiAo;
		float3 ssgiIlSpecular;
		SampleSSGISpecular(dispatchID.xy, specularLobe, ssgiAo, ssgiIlSpecular, normalWS, V);

#		if defined(VR)
		float ssgiAo2;
		float3 ssgiIlSpecular2;
		SampleSSGISpecular(pixCoord2, specularLobe, ssgiAo2, ssgiIlSpecular2, normalWS, V);
		float4 ssgiMixed = Stereo::BlendEyeColors(uv1Mono, float4(ssgiIlSpecular, ssgiAo), uv2Mono, float4(ssgiIlSpecular2, ssgiAo2));
		ssgiAo = ssgiMixed.a;
		ssgiIlSpecular = ssgiMixed.rgb;
#		endif

		finalIrradiance = (finalIrradiance * ssgiAo) + ssgiIlSpecular;
#	endif

		color += reflectance * finalIrradiance;
	}

#endif

	color = Color::LinearToGamma(color);

#if defined(DEBUG)

#	if defined(VR)
	uv.x += (eyeIndex ? 0.1 : -0.1);
#	endif  // VR

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

	MainRW[dispatchID.xy] = float4(color, 1.0);
	NormalTAAMaskSpecularMaskRW[dispatchID.xy] = float4(GBuffer::EncodeNormalVanilla(normalVS), 0.0, 0.0);
}