RWTexture2D<float> BlendedDepthTexture : register(u0);
RWTexture2D<unorm float> BlendedDepthTexture16 : register(u1);

Texture2D<unorm float> MainDepthTexture : register(t0);
#if !defined(MERGE)
Texture2D<unorm float> TerrainDepthTexture : register(t1);
#endif

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
#if defined(MERGE)
	// Fold a depth written after the blend into the accumulator. Read it through the UAV so the same texture is not bound as SRV and UAV at once.
	float other = BlendedDepthTexture[DTid.xy];
#else
	float other = TerrainDepthTexture[DTid.xy];
#endif
	float mixedDepth = min(MainDepthTexture[DTid.xy], other);
	BlendedDepthTexture[DTid.xy] = mixedDepth;
	BlendedDepthTexture16[DTid.xy] = mixedDepth;
}
