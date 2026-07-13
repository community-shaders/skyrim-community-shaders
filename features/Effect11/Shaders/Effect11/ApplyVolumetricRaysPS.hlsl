#include "Common/SharedData.hlsli"

#if defined(IBL)
#	define IBL_DEFERRED
#	include "IBL/IBL.hlsli"
#endif

Texture2D<float> BlurredShadowTexture : register(t0);
// Half-res depth written by the raymarch pass (the depth each texel actually marched with).
Texture2D<float> RaymarchDepthTexture : register(t1);

// Same layout the blur passes use; carries the half-res target dimensions.
cbuffer VLData : register(b1)
{
	int2 ScreenSize;
	int2 ScreenSizeMin1;
}

struct VS_OUTPUT_POST
{
	float4 pos : SV_POSITION;
	float2 txcoord0 : TEXCOORD0;
};

// Joint bilateral upsample of the half-res scattering to full resolution: four bilinear taps
// weighted by depth similarity (linearized), renormalized. Where all taps lie on the same
// surface this degrades to plain bilinear; across depth discontinuities the nearest-depth
// tap dominates, preventing halos around geometry edges.
float UpsampleScattering(float2 fullResPixel, float fullResDepth)
{
	float2 halfPixel = fullResPixel * 0.5 - 0.5;
	int2 basePixel = int2(floor(halfPixel));
	float2 fraction = halfPixel - basePixel;

	const float2 offsets[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
	float4 bilinearWeights = float4(
		(1.0 - fraction.x) * (1.0 - fraction.y),
		fraction.x * (1.0 - fraction.y),
		(1.0 - fraction.x) * fraction.y,
		fraction.x * fraction.y);

	float referenceDepth = SharedData::GetScreenDepth(fullResDepth);

	float weightedSum = 0.0;
	float weightSum = 0.0;
	[unroll]
	for (uint i = 0; i < 4; i++) {
		int2 tap = clamp(basePixel + int2(offsets[i]), int2(0, 0), ScreenSizeMin1);
		float tapDepth = SharedData::GetScreenDepth(RaymarchDepthTexture[tap]);
		float relativeDelta = abs(referenceDepth - tapDepth) / max(referenceDepth, 1e-4);
		float weight = bilinearWeights[i] * rcp(0.01 + relativeDelta);
		weightedSum += weight * BlurredShadowTexture[tap];
		weightSum += weight;
	}
	return weightedSum / weightSum;
}

float4 main(VS_OUTPUT_POST input) : SV_Target0
{
	float2 uv = input.txcoord0;

	float depth = SharedData::GetDepth(uv);
	float volumetricShadow = UpsampleScattering(input.pos.xy, depth);

	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse, positionCS);
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
