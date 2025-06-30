#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "Common/VR.hlsli"

Texture2D<float3> AlbedoTexture : register(t0);
Texture2D<float3> NormalRoughnessTexture : register(t1);
Texture2D<float> DepthTexture : register(t2);
Texture2D<float3> MasksTexture : register(t9);

#if defined(SKYLIGHTING)
#	include "Skylighting/Skylighting.hlsli"

Texture3D<sh2> SkylightingProbeArray : register(t3);
Texture2DArray<float3> stbn_vec3_2Dx1D_128x128x64 : register(t4);

#endif

#if defined(SSGI)
Texture2D<float> SsgiAoTexture : register(t5);
Texture2D<float4> SsgiYTexture : register(t6);
Texture2D<float2> SsgiCoCgTexture : register(t7);
#endif

#if defined(IBL)
#	define IBL_AMBIENTCOMPOSITE
#	include "IBL/IBL.hlsli"
#endif

RWTexture2D<float4> MainRW : register(u0);
#if defined(SSGI)
RWTexture2D<float3> DiffuseAmbientRW : register(u1);
void SampleSSGI(uint2 pixCoord, float3 normalWS, out float ao, out float3 il)
{
	ao = 1 - SsgiAoTexture[pixCoord];
	float4 ssgiIlYSh = SsgiYTexture[pixCoord];
	// without ZH hallucination
	// float ssgiIlY = SphericalHarmonics::FuncProductIntegral(ssgiIlYSh, SphericalHarmonics::EvaluateCosineLobe(normalWS));
	float ssgiIlY = SphericalHarmonics::SHHallucinateZH3Irradiance(ssgiIlYSh, normalWS);
	float2 ssgiIlCoCg = SsgiCoCgTexture[pixCoord];
	il = max(0, Color::YCoCgToRGB(float3(ssgiIlY, ssgiIlCoCg)));
}
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float3 diffuseColor = MainRW[dispatchID.xy].xyz;
	float3 albedo = AlbedoTexture[dispatchID.xy];
#if defined(SSS)
	float3 mask = MasksTexture[dispatchID.xy];
	if (mask.x > 0.001) {
		albedo = float3(1.0, 1.0, 1.0);
	}
#endif

	float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

	float3 directionalAmbientColor = max(0, mul(SharedData::DirectionalAmbient, float4(normalWS, 1.0)));

#if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL) {
		directionalAmbientColor *= SharedData::iblSettings.DALCAmount;
		directionalAmbientColor += Color::Saturation(ImageBasedLighting::GetDiffuseIBL(-normalWS), SharedData::iblSettings.IBLSaturation) * SharedData::iblSettings.DiffuseIBLScale;
	}
#endif

	float3 linAlbedo = Color::GammaToLinear(albedo);
	float3 linDirectionalAmbientColor = Color::GammaToLinear(directionalAmbientColor);
#if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL) {
		linDirectionalAmbientColor = directionalAmbientColor;
	}
#endif
	float3 linDiffuseColor = Color::GammaToLinear(diffuseColor);
	float3 originalDiffuseColor = linDiffuseColor;

	float3 linAmbient = Color::GammaToLinear(albedo * directionalAmbientColor);

	float visibility = 1.0;
#if defined(SKYLIGHTING)
	if (!SharedData::InInterior) {
		float rawDepth = DepthTexture[dispatchID.xy];
		float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, rawDepth, 1);
		float4 positionMS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
		positionMS.xyz = positionMS.xyz / positionMS.w;
#	if defined(VR)
		positionMS.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#	endif
		float3 skylightingNormal = normalize(float3(normalWS.xy, max(0, normalWS.z)));

		sh2 skylightingSH = Skylighting::sample(SharedData::skylightingSettings, SkylightingProbeArray, stbn_vec3_2Dx1D_128x128x64, dispatchID.xy, positionMS.xyz, normalWS);
		float skylightingDiffuse = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(skylightingNormal)) / Math::PI;
		skylightingDiffuse = saturate(skylightingDiffuse);

		skylightingDiffuse = lerp(1.0, skylightingDiffuse, Skylighting::getFadeOutFactor(positionMS.xyz));

		skylightingDiffuse *= 1.0 + saturate(normalWS.z) * (1.0 - SharedData::skylightingSettings.MinDiffuseVisibility);

		skylightingDiffuse = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylightingDiffuse);

		visibility = skylightingDiffuse;
	}
#endif

#if defined(SSGI)
#	if defined(VR)
	float3 uvF = float3((dispatchID.xy + 0.5) * SharedData::BufferDim.zw, DepthTexture[dispatchID.xy]);  // calculate high precision uv of initial eye
	float3 uv2 = Stereo::ConvertStereoUVToOtherEyeStereoUV(uvF, eyeIndex, false);                        // calculate other eye uv
	float3 uv1Mono = Stereo::ConvertFromStereoUV(uvF, eyeIndex);
	float3 uv2Mono = Stereo::ConvertFromStereoUV(uv2, (1 - eyeIndex));
	uint2 pixCoord2 = (uint2)(uv2.xy / SharedData::BufferDim.zw - 0.5);
#	endif

	float ssgiAo;
	float3 ssgiIl;
	SampleSSGI(dispatchID.xy, normalWS, ssgiAo, ssgiIl);

#	if defined(VR)
	float ssgiAo2;
	float3 ssgiIl2;
	SampleSSGI(pixCoord2, normalWS, ssgiAo2, ssgiIl2);

	float4 ssgiMixed = Stereo::BlendEyeColors(uv1Mono, float4(ssgiIl, ssgiAo), uv2Mono, float4(ssgiIl2, ssgiAo2));
	ssgiAo = ssgiMixed.a;
	ssgiIl = ssgiMixed.rgb;
#	endif

	visibility *= ssgiAo;

#	if defined(INTERIOR)
	linDiffuseColor *= ssgiAo;
#	else
	linDiffuseColor *= lerp(ssgiAo, 1.0, 0.5);
#	endif

	linDiffuseColor += ssgiIl * linAlbedo;
#endif

	linAmbient *= visibility;
	diffuseColor = Color::LinearToGamma(linDiffuseColor);
	directionalAmbientColor = Color::LinearToGamma(linDirectionalAmbientColor * visibility);

	diffuseColor = diffuseColor + directionalAmbientColor * albedo;

#if defined(SSGI)
	DiffuseAmbientRW[dispatchID.xy] = Color::GammaToLinear(diffuseColor - originalDiffuseColor);
#endif

	MainRW[dispatchID.xy] = float4(diffuseColor, 1);
};