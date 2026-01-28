Texture2DArray<float> InputTexture : register(t0);
RWTexture2DArray<unorm float> OutputTexture : register(u0);

SamplerState PointSampler : register(s0);

groupshared float g_scratchDepths[8][8];

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID
								: SV_DispatchThreadID, uint2 groupThreadID
								: SV_GroupThreadID) {
	// MIP 0 -> 1: each thread gathers a 2x2 block and computes max
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 1.0) / float2(inputW, inputH);

	float4 depths4 = InputTexture.GatherRed(PointSampler, float3(uv, dispatchThreadID.z));
	float depth0 = depths4.w;
	float depth1 = depths4.z;
	float depth2 = depths4.x;
	float depth3 = depths4.y;

	float dm1 = max(max(depth0, depth1), max(depth2, depth3));
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 1 -> 2: 2x2 reduction in shared memory (4x4 total)
	[branch]
	if (all((groupThreadID.xy % 2) == 0)) {
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
		float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

		float dm2 = max(max(inTL, inTR), max(inBL, inBR));
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;

		uint3 outCoord = uint3(dispatchThreadID.xy / 2, dispatchThreadID.z);
		uint w, h, slices;
		OutputTexture.GetDimensions(w, h, slices);
		if (outCoord.x < w && outCoord.y < h)
			OutputTexture[outCoord] = dm2;
	}
}