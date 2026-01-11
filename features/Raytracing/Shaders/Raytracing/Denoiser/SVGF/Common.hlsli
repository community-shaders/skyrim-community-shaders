#ifndef SVGF_COMMON_HLSI
#define SVGF_COMMON_HLSI

#include "Common/FrameBuffer.hlsli"
#include "Common/Color.hlsli"
#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"
#include "Raytracing/Includes/Common.hlsli"

cbuffer RenderResCB : register(b0)
{
    uint2 Resolution;
    float2 ResolutionRcp;
};

cbuffer SVGFCB : register(b1)
{
    SVGF Frame;
};

Texture2D<float4> NormalRoughnessTexture	: register(t2);

SamplerState      LinearSampler				: register(s0);

#define VAR_EPSILON (0.00001f)

void GetNormalRoughness(uint2 dtid, out float3 normal, out float roughness)
{
    float4 normalRoughness = NormalRoughnessTexture[dtid];
    // Normal is in world space
    normal = normalRoughness.xyz;
    roughness = normalRoughness.w;
}

void GetNormalRoughness(Texture2D<float4> NormalRoughness, uint2 dtid, out float3 normal, out float roughness)
{
    float4 normalRoughness = NormalRoughness[dtid];
    // Normal is in world space
    normal = normalRoughness.xyz;
    roughness = normalRoughness.w;
}

float CalculateWeight(float depthCenter, float depthP, float phiD, float3 normalCenter, float3 normalP, float phiN, float luminanceCenter, float luminanceP, float phiL)
{
	// Depth weight
	float weightDepth = exp(-abs(depthCenter - depthP) / max(phiD, VAR_EPSILON));

	// Normal weight
	float weightNormal = pow(max(0.0f, dot(normalCenter, normalP)), phiN);

	// Luminance weight
	float weightLuminance = exp(-abs(luminanceCenter - luminanceP) / max(phiL, VAR_EPSILON));

	return weightDepth * weightNormal * weightLuminance;
}

float2 ReprojectUV(Texture2D<float2> MotionTexture, in float2 uv, in float depth, in uint eyeIndex)
{
	// Camera motion for pixel (in ScreenPos space).
	float2 thisScreen = (uv.xy - 0.5f) * float2(2.0f, -2.0f);
	
	float4 thisClip = float4(thisScreen, depth, 1);
	
    float4 thisView = mul(FrameBuffer::CameraProjUnjitteredInverse[eyeIndex], thisClip);
    thisView.xyz = thisView.xyz / thisView.w;
	
    float4 thisWorld = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(thisView.xyz, 1.0f));
    thisWorld.xyz = (thisWorld.xyz / thisWorld.w) + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
	
	float4 prevClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered[eyeIndex], float4(thisWorld.xyz, 1.0f));
	
	float2 prevScreen = prevClip.xy / prevClip.w;

	float2 velocity = MotionTexture.SampleLevel(LinearSampler, uv.xy * FrameBuffer::DynamicResolutionParams1.xy, 0).xy;

	prevScreen = thisClip.xy + velocity * float2(2.f, -2.f);

	return prevScreen.xy * float2(0.5f, -0.5f) + 0.5f;
}

float2 ReprojectUV2(Texture2D<float2> MotionTexture, in float2 uv, in float viewDepth, in uint eyeIndex)
{
	float2 velocity = MotionTexture.SampleLevel(LinearSampler, uv, 0).xy;
    return uv + velocity / viewDepth;
}

float2 ReprojectUVSimple(Texture2D<float2> MotionTexture, in float2 uv)
{
	float2 velocity = MotionTexture.SampleLevel(LinearSampler, uv, 0).xy;
    return uv + velocity;
}

#endif