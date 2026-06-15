#include "Common/Color.hlsli"
#include "Raytracing/Includes/Common.hlsli"

Texture2D<float4> GIInput : register(t0);
RWTexture2D<float4> MainOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint2 id : SV_DispatchThreadID) {
	if (any(id >= DynamicResolution))
		return;

    const float3 gi = GIInput[id.xy].rgb;
    MainOutput[id.xy] = MainOutput[id.xy] + float4(ENABLE_LL ? gi : Color::LinearToSrgb(gi), 0.0f);
}