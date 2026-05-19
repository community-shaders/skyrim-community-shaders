#include "Common/Color.hlsli"
#include "Raytracing/Includes/Common.hlsli"

Texture2D<float4> GIInput : register(t0);
RWTexture2D<float4> MainOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint2 id : SV_DispatchThreadID) {
	if (any(id >= DynamicResolution))
		return;

	MainOutput[id.xy] = MainOutput[id.xy] + float4(Color::LinearToSrgb(GIInput[id.xy].rgb), 0.0f);
}