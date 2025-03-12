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

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float3 diffuseColor = MainRW[dispatchID.xy];
	float3 albedo = AlbedoTexture[dispatchID.xy];

	float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

	float3 directionalAmbientColor = mul(SharedData::DirectionalAmbient, float4(normalWS, 1.0));

	float3 linAlbedo = Color::GammaToLinear(albedo);
	float3 linDirectionalAmbientColor = Color::GammaToLinear(directionalAmbientColor);
	float3 linDiffuseColor = Color::GammaToLinear(diffuseColor);

	float3 linAmbient = Color::GammaToLinear(albedo * directionalAmbientColor);

	float visibility = 1.0;
#if defined(SKYLIGHTING)
	float rawDepth = DepthTexture[dispatchID.xy];
	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, rawDepth, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
	positionMS.xyz = positionMS.xyz / positionMS.w;
#	if defined(VR)
	positionMS.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#	endif

	sh2 skylighting = Skylighting::sample(SharedData::skylightingSettings, SkylightingProbeArray, stbn_vec3_2Dx1D_128x128x64, dispatchID.xy, positionMS.xyz, normalWS);
	float skylightingDiffuse = SphericalHarmonics::FuncProductIntegral(skylighting, SphericalHarmonics::EvaluateCosineLobe(float3(normalWS.xy, normalWS.z * 0.5 + 0.5))) / Math::PI;
	skylightingDiffuse = lerp(1.0, skylightingDiffuse, Skylighting::getFadeOutFactor(positionMS.xyz));
	skylightingDiffuse = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylightingDiffuse);

	visibility = skylightingDiffuse;
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

	float clampedLinAlbedo = min(linAlbedo, 0.5);
	DiffuseAmbientRW[dispatchID.xy] = linAmbient * visibility + clampedLinAlbedo * ssgiIl;
	linDiffuseColor += ssgiIl * linAlbedo;
#endif

	linAmbient *= visibility;
	diffuseColor = Color::LinearToGamma(linDiffuseColor);
	directionalAmbientColor = Color::LinearToGamma(linDirectionalAmbientColor * visibility);

	diffuseColor = diffuseColor + directionalAmbientColor * albedo;

	MainRW[dispatchID.xy] = float4(diffuseColor, 1);
};