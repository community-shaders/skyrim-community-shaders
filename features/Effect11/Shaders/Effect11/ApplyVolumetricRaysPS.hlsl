#include "Common/SharedData.hlsli"

#if defined(IBL)
#	define IBL_DEFERRED
#	include "IBL/IBL.hlsli"
#endif

Texture2D<float> BlurredShadowTexture : register(t0);

struct VS_OUTPUT_POST
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

float4 main(VS_OUTPUT_POST input) : SV_Target0
{
	float2 uv = input.txcoord0;
	uint eyeIndex = 0;

	float volumetricShadow = BlurredShadowTexture.Load(int3(input.pos.xy, 0));

	float depth = SharedData::GetDepth(uv);
	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
	positionMS.xyz /= positionMS.w;

	float3 viewDirection = normalize(positionMS.xyz);

	float phase = dot(viewDirection, SharedData::SunDirection.xyz) * 0.5 + 0.5;
	float3 lightColor = SharedData::SunColor.xyz * phase;

#if defined(IBL)
	float3 ibl = ImageBasedLighting::GetSkyIBL(float3(0, 0, -1));
	ibl = lerp(dot(ibl, 1.0 / 3.0), ibl, 2.0);
	lightColor += ibl * SharedData::enbSettings.VolumetricRaysSkyColorAmount;
#endif

	float3 volumetricColor = volumetricShadow * lightColor * SharedData::enbSettings.VolumetricRaysIntensity * SharedData::SunColor.w;

	return float4(volumetricColor, 0);
}
