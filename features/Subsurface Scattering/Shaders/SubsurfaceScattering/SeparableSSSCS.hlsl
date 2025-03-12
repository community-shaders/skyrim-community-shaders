RWTexture2D<float4> SSSRW : register(u0);

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> DepthTexture : register(t1);
Texture2D<float4> MaskTexture : register(t2);

#define SSSS_N_SAMPLES 21

cbuffer PerFrameSSS : register(b1)
{
	float4 Kernels[SSSS_N_SAMPLES + SSSS_N_SAMPLES];
	float4 BaseProfile;
	float4 HumanProfile;
	float SSSS_FOVY;
};

#include "Common/Color.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#include "SubsurfaceScattering/SeparableSSS.hlsli"

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	float2 texCoord = (DTid.xy + 0.5) * SharedData::BufferDim.zw;

#if defined(HORIZONTAL)

	float sssAmount = MaskTexture[DTid.xy].x;
	bool humanProfile = MaskTexture[DTid.xy].y == sssAmount;

	float4 color = SSSSBlurCS(DTid.xy, texCoord, float2(1.0, 0.0), sssAmount, humanProfile);
	SSSRW[DTid.xy] = max(0, color);

#else

	float sssAmount = MaskTexture[DTid.xy].x;
	bool humanProfile = MaskTexture[DTid.xy].y == sssAmount;

	float4 color = SSSSBlurCS(DTid.xy, texCoord, float2(0.0, 1.0), sssAmount, humanProfile);
	color.rgb = Color::LinearToGamma(color.rgb);
	SSSRW[DTid.xy] = float4(color.rgb, 1.0);

#endif
}
