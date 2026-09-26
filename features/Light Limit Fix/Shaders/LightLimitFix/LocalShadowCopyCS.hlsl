// Copies one freshly rendered engine point-light shadow map slice into the Light Limit Fix
// local shadow cache, min-downsampling when the cache is smaller than the engine map so
// occluders never shrink.

Texture2DArray<float> SourceShadowMaps : register(t0);
RWTexture2DArray<float> CacheShadowMaps : register(u0);

cbuffer CopyCB : register(b0)
{
	uint SourceSlice;
	uint TargetSlice;
	uint Scale;
	uint TargetSize;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
	if (any(dispatchThreadId.xy >= TargetSize.xx))
		return;

	uint2 sourceBase = dispatchThreadId.xy * Scale;
	float depth = 1.0;
	for (uint y = 0; y < Scale; y++) {
		for (uint x = 0; x < Scale; x++) {
			depth = min(depth, SourceShadowMaps.Load(int4(sourceBase + uint2(x, y), SourceSlice, 0)));
		}
	}

	CacheShadowMaps[uint3(dispatchThreadId.xy, TargetSlice)] = depth;
}
