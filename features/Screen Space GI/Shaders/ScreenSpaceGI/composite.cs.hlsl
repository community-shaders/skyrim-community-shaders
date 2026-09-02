#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "NRD/NRDReblurSH.hlsli"

Texture2D<unorm float3> AlbedoTexture : register(t0);
// 24/32-bit depth: TerrainBlending ON -> R32_FLOAT (no unorm),
// OFF -> R24_UNORM_X8_TYPELESS game depth (unorm).
#if defined(TERRAIN_BLENDING)
Texture2D<float> DepthTexture : register(t2);
#else
Texture2D<unorm float> DepthTexture : register(t2);
#endif

Texture2D<float4> SsgiTexture : register(t3);
#if defined(SSGI_SH)
Texture2D<float4> SsgiSH1Texture : register(t4);
#endif

RWTexture2D<float4> MainRW : register(u0);

float3 SampleSSGIIL(uint2 pixCoord)
{
#if defined(SSGI_SH)
	NRD_SG sg = REBLUR_BackEnd_UnpackSh(SsgiTexture[pixCoord], SsgiSH1Texture[pixCoord]);
	// The tracer stores cosine-convolved diffuse illumination in SH0. SH1 is a
	// directional moment for denoising, not an incident lobe to shade again.
	float3 radiance = _NRD_YCoCgToLinear(float3(sg.c0, sg.chroma));
#else
	float normHitDist;
	float3 radiance;
	REBLUR_BackEnd_UnpackRadianceAndNormHitDist(SsgiTexture[pixCoord], radiance, normHitDist);
#endif
	return max(0, radiance);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= uint2(SharedData::BufferDim.xy)) || SharedData::ssgiSettings.EnableIL == 0)
		return;

	float depth = DepthTexture[dispatchID.xy];
	if (depth >= 1.0 - 1e-6)
		return;

	float3 linAlbedo = Color::IrradianceToLinear(AlbedoTexture[dispatchID.xy] / Color::PBRLightingScale);
	float3 ssgiIl = SampleSSGIIL(dispatchID.xy);

	float4 mainColor = MainRW[dispatchID.xy];
	float3 linDiffuseColor = Color::IrradianceToLinear(mainColor.xyz);
	linDiffuseColor += ssgiIl * linAlbedo;
	MainRW[dispatchID.xy] = float4(Color::IrradianceToGamma(linDiffuseColor), mainColor.a);
}
