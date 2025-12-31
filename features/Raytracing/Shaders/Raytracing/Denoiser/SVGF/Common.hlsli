#ifndef SVGF_COMMON_HLSI
#define SVGF_COMMON_HLSI

#include "Common/FrameBuffer.hlsli"
#include "Common/Color.hlsli"
#include "Raytracing/Denoiser/SVGF/SVGF.hlsli"

cbuffer RenderResCB : register(b0)
{
    uint2 Resolution;
    float2 ResolutionRcp;
};

ConstantBuffer<SVGF>	Frame					: register(b1);

Texture2D<float4>		NormalRoughnessTexture  : register(t2);
Texture2D<float4>		SSRColorTexture			: register(t3);
Texture2D<float>		DepthTexture			: register(t4);

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

float CalculateWeight(float depthCenter, float depthP, float phiD, float3 normalCenter, float3 normalP, float phiN,
					  float luminanceCenter, float luminanceP, float phiL)
{
	float epsilon = 0.0000001;

	// Depth weight
	float difference = abs(depthCenter - depthP);
	float weightDepth = (phiD == 0) ? 0.f : difference / max(phiD, epsilon);

	// Normal weight
	float weightNormal = pow(max(0.f, dot(normalCenter, normalP)), phiN);

	// Luminance weight
	float weightLuminance = abs(luminanceCenter - luminanceP) / phiL;

	float weight = exp(-weightDepth - weightLuminance) * weightNormal;
	return weight;
}

void ReprojectHit(Texture2D<float2> MotionTexture, SamplerState s, float3 hitUVz, uint eyeIndex, out float2 outPrevUV)
{
	// Camera motion for pixel (in ScreenPos space).
	float2 thisScreen = (hitUVz.xy - 0.5f) * float2(2.0f, -2.0f);
	float4 thisClip = float4(thisScreen, hitUVz.z, 1);
    float4 thisView = mul(FrameBuffer::CameraProjUnjitteredInverse[eyeIndex], thisClip);
    thisView.xyz = thisView.xyz / thisView.w;
    float4 thisWorld = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(thisView.xyz, 1.0f));
    thisWorld.xyz = thisWorld.xyz / thisWorld.w;
	float4 prevClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered[eyeIndex], float4(thisWorld.xyz, 1.0f));
	float2 prevScreen = prevClip.xy / prevClip.w;

	float2 velocity = MotionTexture.SampleLevel(s, hitUVz.xy * FrameBuffer::DynamicResolutionParams1.xy, 0).xy;

	prevScreen = thisClip.xy + velocity * float2(2.f, -2.f);

	float2 prevUV = prevScreen.xy * float2(0.5f, -0.5f) + 0.5f;

	outPrevUV = prevUV;
}

#endif