RWTexture2D<float4> OutputRW : register(u0);

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> AlbedoTexture : register(t3);

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "SubsurfaceScattering/SSSCommon.hlsli"

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	if (any(DTid.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float4 color = ColorTexture[DTid.xy];
	color.rgb = SSSRemoveAlbedo(color.rgb, AlbedoTexture[DTid.xy].rgb, ScatterMode);
	color.rgb = Color::IrradianceToLinear(color.rgb);
	OutputRW[DTid.xy] = color;
}
