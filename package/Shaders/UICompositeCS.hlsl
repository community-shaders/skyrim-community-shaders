#include "Common/Color.hlsli"

SamplerState LinearSampler : register(s0);

Texture2D<float4> HDRScene : register(t0);
Texture2D<float4> UIBuffer : register(t1);
Texture2D<float4> BloomTex : register(t2);
Texture2D<float4> AdaptTex : register(t3);
RWTexture2D<float4> Output : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = paperWhite, .y = peakNits, .z = exposure
	float4 parameters1 : packoffset(c1);
	float4 parameters2 : packoffset(c2);  // .z = vanillaEyeAdaptation, .w = vanillaBloom
	float4 parameters3 : packoffset(c3);  // .x = hdrMode (1.0 = HDR, 0.0 = SDR)
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint2 dims;
	Output.GetDimensions(dims.x, dims.y);
	float2 uv = (float2(dispatchID.xy) + 0.5) / float2(dims);
	
	float4 scene = HDRScene[dispatchID.xy];
	float4 ui = UIBuffer[dispatchID.xy];
	float3 bloom = BloomTex[dispatchID.xy].rgb;
	float2 adaptValue = AdaptTex.SampleLevel(LinearSampler, uv, 0).xy;
	
	bool hdrMode = parameters3.x > 0.5;
	
	// SDR mode: Vanilla ISHDR has already done ALL processing (adaptation, bloom, exposure, tonemap)
	// Just pass through the final result - don't reprocess!
	if (!hdrMode) {
		Output[dispatchID.xy] = float4(scene.rgb, ui.a);
		return;
	}
	
	// HDR mode: kMAIN is linear float, vanilla processing may be bypassed
	// Apply our own exposure, adaptation, and bloom
	float exposure = parameters0.z;
	bool useVanillaAdaptation = parameters2.z > 0.5;
	bool useVanillaBloom = parameters2.w > 0.5;
	
	// Start with scene - don't apply adaptation here since ISHDR already does it
	// We read from kMAIN which has raw scene data, so we need to apply adaptation
	float3 adaptedScene = scene.rgb;
	if (useVanillaAdaptation && adaptValue.x > 0.0 && adaptValue.y > 0.0)
		adaptedScene *= adaptValue.y / adaptValue.x;
	adaptedScene = max(0.0, adaptedScene);
	
	float3 sceneWithBloom = useVanillaBloom ? (adaptedScene + bloom) : adaptedScene;
	
	// Apply exposure adjustment (user-controlled, default 1.0)
	float3 exposedScene = sceneWithBloom * exposure;
	
	// Output scene in same color space as input (gamma for SDR, linear for HDR)
	// Store UI alpha in output alpha channel for later compositing
	Output[dispatchID.xy] = float4(exposedScene, ui.a);
}
