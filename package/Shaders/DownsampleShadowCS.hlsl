Texture2DArray<float> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

SamplerState PointSampler : register(s0);

#if defined(DOWNSAMPLE_SHADOW_MIP0)
[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
	uint w, h;
	OutputTexture.GetDimensions(w, h);

	if (dispatchThreadID.x < w && dispatchThreadID.y < h) {
		// Each thread gathers a 2x2 block and packs into RGBA
		uint2 pixCoord = dispatchThreadID.xy * 2;

		uint inputW, inputH, inputSlices;
		InputTexture.GetDimensions(inputW, inputH, inputSlices);
		float2 uv = (pixCoord + 1.0) / float2(inputW, inputH);

		float4 depths4 = InputTexture.GatherRed(PointSampler, float3(uv, 1));

		OutputTexture[dispatchThreadID.xy] = depths4;
	}
}
#elif defined(DOWNSAMPLE_SHADOW_MIP1)
groupshared float g_scratchDepths[8][8];

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	// MIP 0 -> 1: each thread gathers a 2x2 block and averages
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 1.0) / float2(inputW, inputH);

	float4 depths4 = InputTexture.GatherRed(PointSampler, float3(uv, 0));

	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dot(depths4, 0.25);

	GroupMemoryBarrierWithGroupSync();

	// MIP 1 -> 2: 2x2 reduction in shared memory (4x4 total)
	[branch]
	if (all((groupThreadID.xy % 2) == 0)) {
		uint2 outCoord = uint2(dispatchThreadID.xy / 2);
		uint w, h, slices;
		OutputTexture.GetDimensions(w, h);
		[branch] if (outCoord.x < w && outCoord.y < h){
			float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
			float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
			float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
			float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];
			OutputTexture[outCoord] = float4(inTL, inTR, inBL, inBR);
		}
	}
}
#else
#error "Error: Missing downsample scale"
#endif
