RWTexture2D<float> BlendedDepthTexture : register(u0);
RWTexture2D<unorm float> BlendedDepthTexture16 : register(u1);

Texture2D<unorm float> MainDepthTexture : register(t0);
#if !defined(MERGE)
Texture2D<unorm float> TerrainDepthTexture : register(t1);
#endif

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
#if defined(MERGE)
	// Read the accumulator through its UAV to avoid overlapping SRV/UAV bindings.
	float other = BlendedDepthTexture[DTid.xy];
#else
	float other = TerrainDepthTexture[DTid.xy];
#endif
	float mixedDepth = min(MainDepthTexture[DTid.xy], other);
	BlendedDepthTexture[DTid.xy] = mixedDepth;
	BlendedDepthTexture16[DTid.xy] = mixedDepth;
}
