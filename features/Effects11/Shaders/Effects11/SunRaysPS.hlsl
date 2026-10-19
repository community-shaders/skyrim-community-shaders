// ENB [RAYS] screen-space shafts from the sun or a moon.
// MASK draws the light's sky disc at half resolution, BLUR smears it radially towards the light,
// and COMPOSITE adds the result to the main target.

#include "Common/ColorManagement.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

SamplerState LinearSampler : register(s0);

#if defined(MASK) && defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

Texture2D<float> RaysTexture : register(t0);

cbuffer SunRaysData : register(b1)
{
	float2 LightUV;
	float2 UVScale;
	float2 UVMax;
	float BlurFactor;
	uint StepCount;
	float3 LightDirection;
	float LightBillboardTan;
	float3 RaysColor;
	float MaskBrightness;
	float MaskExponent;
	uint WeightedSteps;
	uint UseNoise;
	float InvScreenWidth;
}

struct VS_OUTPUT_POST
{
	float4 pos: SV_POSITION;
	float2 txcoord0: TEXCOORD0;
};

static const float SkyDepth = 0.999997;

float SampleRays(float2 uv)
{
	return RaysTexture.SampleLevel(LinearSampler, min(saturate(uv) * UVScale, UVMax), 0);
}

float RadialBlur(float2 uv, float2 target, float noise)
{
	float stepInv = rcp(float(StepCount));
	float shift = -stepInv * noise;
	float rays = 0.0;
	[loop] for (uint i = 0; i < StepCount; i++)
	{
		shift += stepInv;
		float weight = WeightedSteps ? 1.0 - shift : 1.0;
		rays += SampleRays(lerp(uv, target, shift)) * weight;
	}
	return rays * stepInv;
}

float GetNoise(float2 pixel)
{
	return UseNoise ? Random::InterleavedGradientNoise(pixel, SharedData::FrameCount) : 0.5;
}

#if defined(MASK)
float main(VS_OUTPUT_POST input) : SV_Target0
{
	float2 uv = input.txcoord0;
	if (SharedData::GetDepth(uv) < SkyDepth)
		return 0.0;

	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, 0.5, 1);
	float4 positionMS = mul(FrameBuffer::CameraViewProjInverse, positionCS);
	float3 viewDirection = normalize(positionMS.xyz / positionMS.w);

	float cosAngle = dot(viewDirection, LightDirection);
	if (cosAngle <= 0.0)
		return 0.0;

	float tanAngleSq = max(1.0 - cosAngle * cosAngle, 0.0) / (cosAngle * cosAngle);
	float radiusSq = tanAngleSq / max(LightBillboardTan * LightBillboardTan, 1e-8);
	float mask = MaskBrightness * pow(saturate(1.0 - radiusSq), MaskExponent);

#	if defined(CLOUD_SHADOWS)
	mask *= saturate(1.0 - CloudShadows::CloudShadowsTexture.SampleLevel(LinearSampler, viewDirection, 0));
#	endif

	float2 edge = abs(uv * 2.0 - 1.0);
	float vignette = max(edge.x, edge.y);
	return mask * (1.0 - vignette * vignette);
}
#elif defined(BLUR)
float main(VS_OUTPUT_POST input) : SV_Target0
{
	float2 uv = input.txcoord0;
	return RadialBlur(uv, lerp(uv, LightUV, BlurFactor), GetNoise(input.pos.xy));
}
#elif defined(COMPOSITE)
float4 main(VS_OUTPUT_POST input) : SV_Target0
{
	float2 uv = input.txcoord0;
	float3 direction = normalize(float3(LightUV - uv, 0.035));
	float2 target = uv + direction.xy * (InvScreenWidth * float(StepCount) * 6.0);
	float rays = RadialBlur(uv, target, GetNoise(input.pos.xy));
	return float4(rays * ColorManagement::SRGBToWorking(RaysColor), 1.0);
}
#endif
