#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float3> srcRadiance : register(t0);
RWTexture2D<float3> outRadiance0 : register(u0);

SamplerState samplerPointClamp : register(s0);

cbuffer SSRTCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    uint UseDynamicCubemapsAsFallback;
    float Thickness;
    float NormalBias;
    float BRDFBias;
    float OcclusionStrength;
    float CubemapNormalization;

    float2 TexDim;
    float2 RcpTexDim;
    float2 FrameDim;
    float2 RcpFrameDim;
};

float3 RadianceMIPFilter(float3 radiance0, float3 radiance1, float3 radiance2, float3 radiance3)
{
	return (radiance0 + radiance1 + radiance2 + radiance3) * 0.25;
}

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	const uint2 baseCoord = DTid;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

	float4 rad0 = srcRadiance.GatherRed(samplerPointClamp, uv * frameScale);
	float4 rad1 = srcRadiance.GatherGreen(samplerPointClamp, uv * frameScale);
	float4 rad2 = srcRadiance.GatherBlue(samplerPointClamp, uv * frameScale);

	float3 radiance0 = Color::IrradianceToLinear(float3(rad0.w, rad1.w, rad2.w));
	float3 radiance1 = Color::IrradianceToLinear(float3(rad0.z, rad1.z, rad2.z));
	float3 radiance2 = Color::IrradianceToLinear(float3(rad0.x, rad1.x, rad2.x));
	float3 radiance3 = Color::IrradianceToLinear(float3(rad0.y, rad1.y, rad2.y));

	outRadiance0[baseCoord] = RadianceMIPFilter(radiance0, radiance1, radiance2, radiance3);
}
