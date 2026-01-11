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
	
	float exposure = parameters0.z;
	bool useVanillaAdaptation = parameters2.z > 0.5;
	bool useVanillaBloom = parameters2.w > 0.5;
	
	float3 adaptedScene = scene.rgb;
	if (useVanillaAdaptation && adaptValue.x > 0.0 && adaptValue.y > 0.0)
		adaptedScene *= adaptValue.y / adaptValue.x;
	adaptedScene = max(0.0, adaptedScene);
	
	float3 sceneWithBloom = useVanillaBloom ? (adaptedScene + bloom) : adaptedScene;
	float3 exposedScene = sceneWithBloom * exposure;
	
	float3 uiLinear = Color::GammaToLinearSafe(ui.rgb);
	float3 composited = uiLinear * ui.a + exposedScene * (1.0 - ui.a);
	
	Output[dispatchID.xy] = float4(composited, scene.a);
}
