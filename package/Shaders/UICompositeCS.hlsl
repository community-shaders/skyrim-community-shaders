#include "Common/Color.hlsli"

Texture2D<float4> HDRScene : register(t0);
Texture2D<float4> UIBuffer : register(t1);
Texture2D<float4> BloomTex : register(t2);
RWTexture2D<float4> Output : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = paperWhite, .y = peakNits, .z = exposure
	float4 parameters1 : packoffset(c1);
	float4 parameters2 : packoffset(c2);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = HDRScene[dispatchID.xy];
	float4 ui = UIBuffer[dispatchID.xy];
	float3 bloom = BloomTex[dispatchID.xy].rgb;
	
	float paperWhite = parameters0.x;
	float exposure = parameters0.z;
	
	// Apply bloom to scene (additive, same as vanilla ISHDR)
	float3 sceneWithBloom = scene.rgb + bloom;
	
	// Apply user exposure
	float3 exposedScene = sceneWithBloom * exposure;
	
	// Convert UI from gamma to linear space
	float3 uiLinear = Color::GammaToLinearSafe(ui.rgb);
	
	// Scale UI to paper white level (SDR UI is authored for 80 nits reference)
	uiLinear *= paperWhite / 80.0;
	
	// Premultiplied alpha blend: src * alpha + dst * (1 - alpha)
	float3 composited = uiLinear * ui.a + exposedScene * (1.0 - ui.a);
	
	Output[dispatchID.xy] = float4(composited, scene.a);
}
