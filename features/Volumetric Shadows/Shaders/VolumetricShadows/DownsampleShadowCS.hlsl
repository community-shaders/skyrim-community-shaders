Texture2DArray<float> InputTexture : register(t0);
Texture2DArray<float> ESRAMShadow : register(t1);
RWTexture2D<float2> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

float2 GetVSMMoments(in float depth)
{
	return float2(depth, depth * depth);
}

float2 ReduceMoments(float2 a, float2 b, float2 c, float2 d)
{
	return (a + b + c + d) * 0.25;
}

groupshared float2 g_scratchDepths[8][8];

#if defined(DOWNSAMPLE_SHADOW_MIP0)
static const uint CASCADE = 1;
#elif defined(DOWNSAMPLE_SHADOW_MIP1)
static const uint CASCADE = 0;
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	uint outputW, outputH;
	OutputTexture.GetDimensions(outputW, outputH);

	// Determine reduction levels from input/output ratio
	// Gather handles 2x, each group reduction handles another 2x
	uint totalReduction = inputW / outputW;
	uint groupReductions = 0;
	if (totalReduction >= 4)
		groupReductions = 1;
	if (totalReduction >= 8)
		groupReductions = 2;
	if (totalReduction >= 16)
		groupReductions = 3;

	// Gather from shadow cascades and mix with ESRAM shadow
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, CASCADE));
	float4 esramDepths = ESRAMShadow.GatherRed(LinearSampler, float3(uv, CASCADE));
	depths = min(depths, esramDepths);

	float2 vsmDepth = 0;
	for (uint i = 0; i < 4; i++)
		vsmDepth += GetVSMMoments(depths[i]);
	vsmDepth *= 0.25;

	g_scratchDepths[groupThreadID.x][groupThreadID.y] = vsmDepth;

	GroupMemoryBarrierWithGroupSync();

	// First reduction: 2x2
	if (groupReductions >= 1) {
		if (all((groupThreadID.xy % 2) == 0)) {
			uint2 tid = groupThreadID.xy;
			g_scratchDepths[tid.x][tid.y] = ReduceMoments(
				g_scratchDepths[tid.x + 0][tid.y + 0],
				g_scratchDepths[tid.x + 1][tid.y + 0],
				g_scratchDepths[tid.x + 0][tid.y + 1],
				g_scratchDepths[tid.x + 1][tid.y + 1]);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// Second reduction: 4x4
	if (groupReductions >= 2) {
		if (all((groupThreadID.xy % 4) == 0)) {
			uint2 tid = groupThreadID.xy;
			g_scratchDepths[tid.x][tid.y] = ReduceMoments(
				g_scratchDepths[tid.x + 0][tid.y + 0],
				g_scratchDepths[tid.x + 2][tid.y + 0],
				g_scratchDepths[tid.x + 0][tid.y + 2],
				g_scratchDepths[tid.x + 2][tid.y + 2]);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// Third reduction: 8x8
	if (groupReductions >= 3) {
		if (all(groupThreadID.xy == 0)) {
			g_scratchDepths[0][0] = ReduceMoments(
				g_scratchDepths[0][0],
				g_scratchDepths[4][0],
				g_scratchDepths[0][4],
				g_scratchDepths[4][4]);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// Write output - only threads aligned to the output grid
	uint outputDiv = max(totalReduction / 2, 1);
	if (all((groupThreadID.xy % outputDiv) == 0)) {
		OutputTexture[dispatchThreadID.xy / outputDiv] = g_scratchDepths[groupThreadID.x][groupThreadID.y];
	}
}
