RWTexture2D<float4> OutputRW : register(u0);

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> AlbedoTexture : register(t3);
#if defined(BURLEY)
Texture2D<float4> MaskTexture : register(t2);
#endif

#include "Common/Color.hlsli"
#include "Common/ColorManagement.hlsli"
#include "Common/SharedData.hlsli"
#include "SubsurfaceScattering/SSSCommon.hlsli"

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	if (any(DTid.xy >= uint2(SharedData::BufferDim.xy)))
		return;

#if defined(BURLEY)
	if (!(MaskTexture[DTid.xy].x > 0.0))
		return;
#endif

	float4 color = ColorTexture[DTid.xy];
	float3 linearColor = ColorManagement::SceneToLinear(color.rgb);
	float3 albedo = SSSDecodeAlbedo(AlbedoTexture[DTid.xy].rgb);
	color.rgb = SSSRemoveAlbedo(linearColor, albedo, ScatterMode);
	OutputRW[DTid.xy] = color;
}
