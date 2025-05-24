#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
SamplerState BlurredSampler : register(s1);
SamplerState DepthSampler : register(s2);
SamplerState AvgDepthSampler : register(s3);
SamplerState MaskSampler : register(s4);

Texture2D<float4> ImageTex : register(t0);
Texture2D<float4> BlurredTex : register(t1);
Texture2D<float4> DepthTex : register(t2);
Texture2D<float4> AvgDepthTex : register(t3);
Texture2D<float4> MaskTex : register(t4);

cbuffer PerGeometry : register(b2)
{
	float4 invScreenRes : packoffset(c0);  // inverse render target width and height in xy
	float4 params : packoffset(c1);        // DOF near range in x, far range in y
	float4 params2 : packoffset(c2);       // DOF near blur in x, far blur in w
	float4 params3 : packoffset(c3);       // 1 / (far - near) in z, near / (far - near) in w
	float4 params4 : packoffset(c4);
	float4 params5 : packoffset(c5);
	float4 params6 : packoffset(c6);
	float4 params7 : packoffset(c7);
};

void CheckOffsetDepth(float2 center, float2 offset, inout float crossSection,
	inout float totalDepth)
{
	float depth = DepthTex
	                  .Sample(DepthSampler, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(
												invScreenRes.xy * offset + center))
	                  .x;

	float crossSectionDelta = 0;
	if (depth > 0.999998987) {
		crossSectionDelta = (1. / 9.);
	}
	crossSection += crossSectionDelta;
	totalDepth += depth;
}

float GetFinalDepth(float depth, float near, float far)
{
	return (2 * near * far) / ((far + near) - (depth * 2 - 1) * (far - near));
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 adjustedTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float3 imageColor = ImageTex.Sample(ImageSampler, adjustedTexCoord).xyz;
	float3 blurColor = BlurredTex.Sample(BlurredSampler, adjustedTexCoord).xyz;

	float mask = 1;
	float4 dofParams = params;
	float4 dofParams2 = params2;
#	if defined(MASKED)
	mask = MaskTex.Sample(MaskSampler, adjustedTexCoord).x;
	dofParams = lerp(params, params6, mask);
	dofParams2 = lerp(params2, params7, mask);
#	endif

	float2 dofBlurRange = float2(dofParams2.x, dofParams.x);
	float focusDistance = dofParams.y;

#	if !defined(MASKED)
	if (params3.z > 0) {
		focusDistance = AvgDepthTex.Sample(AvgDepthSampler, 0).x;
		float depthFactor = saturate(focusDistance * params3.z - params3.w);
		dofBlurRange = lerp(float2(params2.x, params.x), float2(params2.w, params.y), depthFactor);
	}
#	endif

	float depthCC =
		DepthTex.Sample(DepthSampler, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord)).x;

	float crossSection = 0;
	float avgDepth = depthCC;
	bool isTooDeep = false;
	if (dofParams2.w != 0 && depthCC > 0.999998987) {
		crossSection = 1. / 9.;
		float totalDepth = depthCC;
		CheckOffsetDepth(input.TexCoord, float2(-3, -3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(-3, 0), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(-3, 3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(3, -3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(3, 0), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(3, 3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(0, -3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(0, 3), crossSection, totalDepth);

		avgDepth = totalDepth / 9;
		isTooDeep = avgDepth > 0.999998987;
	}

	float blurFactor = 0;
	float finalDepth = avgDepth;
	if (!isTooDeep && avgDepth > 1e-5) {
		float depth, near, far;
		if (avgDepth <= 0.01) {
			depth = 100 * avgDepth;
			near = params3.x;
			far = params3.y;
		} else {
			depth = 1.01 * avgDepth - 0.01;
			near = dofParams.z;
			far = dofParams.w;
		}
		finalDepth = GetFinalDepth(depth, near, far);

		float dofStrength = 0;
#	if defined(DISTANT)
		dofStrength = (finalDepth - focusDistance) / dofBlurRange.y;
#	else
		if ((focusDistance > finalDepth || mask == 0) && dofParams2.y != 0) {
			dofStrength = (focusDistance - finalDepth) / dofBlurRange.y;
		} else if (finalDepth > focusDistance && dofParams2.z != 0) {
			dofStrength = (finalDepth - focusDistance) / dofBlurRange.y;
		}
#	endif

		blurFactor = saturate(dofStrength) * (dofBlurRange.x * (1 - 0.5 * crossSection));
	}

	float3 finalColor = lerp(imageColor, blurColor, blurFactor);
#	if defined(FOGGED)
	float fogFactor = (params4.w * saturate((finalDepth - params5.y) / (params5.x - params5.y))) * mask;
	finalColor = lerp(finalColor, params4.xyz, fogFactor);
#	endif

	psout.Color = float4(finalColor, 1);

	return psout;
}
#endif
