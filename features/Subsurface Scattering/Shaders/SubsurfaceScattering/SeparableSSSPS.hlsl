Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> DepthTexture : register(t1);
Texture2D<float4> MaskTexture : register(t2);
Texture2D<float4> AlbedoTexture : register(t3);

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
#include "Common/FullscreenVS.hlsl"
#include "Common/SharedData.hlsli"

#if defined(PSHADER)

#	include "SubsurfaceScattering/SeparableSSS.hlsli"

float4 main(VS_OUTPUT input) : SV_Target
{
	uint2 DTid = uint2(input.Position.xy);

	float2 texCoord = input.TexCoord;

	float sssAmount = MaskTexture[DTid].x;
	bool humanProfile = MaskTexture[DTid].y > 0.0;

#	if defined(HORIZONTAL)

	float4 color = SSSSBlurPS(DTid, texCoord, float2(1.0, 0.0), sssAmount, humanProfile);
	return max(0, color);

#	else

	// Non-SSS pixels were passed through unchanged by the H-pass,
	// so they're still in their original gamma+albedo format.
	// Return them directly without applying the SSS albedo reapplication.
	if (sssAmount <= 0.0)
		return ColorTexture[DTid];

	float4 color = SSSSBlurPS(DTid, texCoord, float2(0.0, 1.0), sssAmount, humanProfile);
	// Reapply albedo and convert to gamma
	color.rgb = Color::IrradianceToGamma(color.rgb) * AlbedoTexture[DTid].xyz;
	return float4(color.rgb, 1.0);

#	endif
}

#endif
