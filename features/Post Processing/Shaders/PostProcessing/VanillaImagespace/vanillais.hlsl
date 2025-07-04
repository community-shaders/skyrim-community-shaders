#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);

cbuffer VanillaISCB : register(b1)
{
	float4 Cinematic;
	float4 Fade;
	float4 Tint;
};

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	float4 color = TexColor[DTid.xy];

	if (Cinematic.y + Cinematic.z == 0) {
		RWTexOut[DTid.xy] = color;
		return;
	}

	float luminance = Color::RGBToLuminance2(color.xyz);
	float3 ppColor = color.xyz;

	ppColor = Cinematic.y * lerp(lerp(luminance, ppColor, Cinematic.x), luminance * Tint.xyz, Tint.w);
	ppColor = clamp(pow(clamp(ppColor, 0.0f, 16.0f), pow(2.0f, Cinematic.z - 1.0f)), 0.0f, 16.0f);

	if (Fade.w > 0) {
		ppColor = lerp(ppColor, Fade.xyz, Fade.w);
	}

	RWTexOut[DTid.xy] = float4(ppColor, color.a);
}