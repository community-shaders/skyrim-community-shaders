Texture2DArray<float> InputTexture : register(t0);
RWTexture2D<float2> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

// Compute VSM moments from 4 depth samples
float2 ComputeVSMMoments(float4 depths) {
	float2 moments = 0;
	moments += float2(depths.x, depths.x * depths.x);
	moments += float2(depths.y, depths.y * depths.y);
	moments += float2(depths.z, depths.z * depths.z);
	moments += float2(depths.w, depths.w * depths.w);
	return moments * 0.25;
}

#if defined(DOWNSAMPLE_SHADOW_MIP0)
// Cascade 1: Mip 0->1->2->3 (8x total reduction)
groupshared float2 g_scratchDepths[8][8];

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	// Gather from cascade 1 and compute VSM moments
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, 1));
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = ComputeVSMMoments(depths);

	GroupMemoryBarrierWithGroupSync();

	// First 2x2 reduction
	if (all((groupThreadID.xy % 2) == 0)) {
		uint2 tid = groupThreadID.xy;
		OutputTexture[dispatchThreadID.xy / 2] =
			(g_scratchDepths[tid.x + 0][tid.y + 0] +
			 g_scratchDepths[tid.x + 1][tid.y + 0] +
			 g_scratchDepths[tid.x + 0][tid.y + 1] +
			 g_scratchDepths[tid.x + 1][tid.y + 1]) * 0.25;
	}
}

#elif defined(DOWNSAMPLE_SHADOW_MIP1)
// Cascade 0: Mip 0->1->2->3->4 (16x total reduction)
groupshared float2 g_scratchDepths[8][8];

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	// Gather from cascade 0 and compute VSM moments
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, 0));
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = ComputeVSMMoments(depths);

	GroupMemoryBarrierWithGroupSync();

	// First reduction: 2x2
	if (all((groupThreadID.xy % 2) == 0)) {
		uint2 tid = groupThreadID.xy;
		g_scratchDepths[tid.x][tid.y] =
			(g_scratchDepths[tid.x + 0][tid.y + 0] +
			 g_scratchDepths[tid.x + 1][tid.y + 0] +
			 g_scratchDepths[tid.x + 0][tid.y + 1] +
			 g_scratchDepths[tid.x + 1][tid.y + 1]) * 0.25;
	}

	GroupMemoryBarrierWithGroupSync();

	// Second reduction: 4x4
	if (all((groupThreadID.xy % 4) == 0)) {
		uint2 tid = groupThreadID.xy;
		OutputTexture[dispatchThreadID.xy / 4] =
			(g_scratchDepths[tid.x + 0][tid.y + 0] +
			 g_scratchDepths[tid.x + 2][tid.y + 0] +
			 g_scratchDepths[tid.x + 0][tid.y + 2] +
			 g_scratchDepths[tid.x + 2][tid.y + 2]) * 0.25;
	}
}
#endif
