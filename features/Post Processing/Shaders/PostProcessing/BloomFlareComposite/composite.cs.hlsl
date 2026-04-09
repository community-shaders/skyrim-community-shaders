/// Bloom/Flare Composite pass
/// Combines bloom and/or lens flare results with the main color texture.
/// Uses #ifdef HAS_BLOOM / HAS_LENS_FLARE to control which inputs are sampled.

Texture2D<float4> TexColor : register(t0);

#ifdef HAS_BLOOM
Texture2D<float4> TexBloom : register(t1);
#endif

#ifdef HAS_LENS_FLARE
Texture2D<float4> TexFlare : register(t2);
#endif

RWTexture2D<float4> RWTexOutput : register(u0);

[numthreads(8, 8, 1)] void CSComposite(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexOutput.GetDimensions(dims.x, dims.y);

	if (any(tid >= dims))
		return;

	float3 col = TexColor[tid].rgb;

#ifdef HAS_BLOOM
	col += TexBloom[tid].rgb;
#endif

#ifdef HAS_LENS_FLARE
	col += TexFlare[tid].rgb;
#endif

	RWTexOutput[tid] = float4(col, 1);
}
