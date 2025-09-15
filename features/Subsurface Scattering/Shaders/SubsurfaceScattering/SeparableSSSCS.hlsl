RWTexture2D<float4> SSSRW : register(u0);

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> DepthTexture : register(t1);
Texture2D<float4> MaskTexture : register(t2);
Texture2D<float4> AlbedoTexture : register(t3);
Texture2D<float4> NormalTexture : register(t4);

#define SSSS_N_SAMPLES 21

cbuffer PerFrameSSS : register(b1)
{
	float4 Kernels[SSSS_N_SAMPLES + SSSS_N_SAMPLES];
	float4 BaseProfile;
	float4 HumanProfile;
	float SSSS_FOVY;
	uint BurleySamples;
	uint2 pad;
	float4 MeanFreePathBase;
	float4 MeanFreePathHuman;
};

#include "Common/Color.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#if defined(BURLEY)
#	include "SubsurfaceScattering/Burley.hlsli"
#else
#	include "SubsurfaceScattering/SeparableSSS.hlsli"
#endif

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside screen bounds
	if (any(DTid.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float2 texCoord = (DTid.xy + 0.5) * SharedData::BufferDim.zw;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(texCoord);

#if defined(BURLEY)

	float sssAmount = MaskTexture[DTid.xy].x;
	bool humanProfile = MaskTexture[DTid.xy].y > 0.0;

	float4 color = BurleyNormalizedSS(DTid.xy, texCoord, eyeIndex, sssAmount, humanProfile);
	SSSRW[DTid.xy] = max(0, color);

#elif defined(HORIZONTAL)

	float sssAmount = MaskTexture[DTid.xy].x;
	bool humanProfile = MaskTexture[DTid.xy].y > 0.0;

	float4 color = SSSSBlurCS(DTid.xy, texCoord, float2(1.0, 0.0), sssAmount, humanProfile);
	SSSRW[DTid.xy] = max(0, color);

#else

	float sssAmount = MaskTexture[DTid.xy].x;

	if (sssAmount > 0.0) {
		bool humanProfile = MaskTexture[DTid.xy].y > 0.0;

		float4 color = SSSSBlurCS(DTid.xy, texCoord, float2(0.0, 1.0), sssAmount, humanProfile);
		color.rgb = Color::IrradianceToGamma(color.rgb);
		SSSRW[DTid.xy] = float4(color.rgb, 1.0);
	}

#endif
}
