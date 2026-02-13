Texture2DArray<float> InputTexture : register(t0);
RWTexture2D<float2> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

static const float VSM_MIN_BIAS = 0.5;

float2 GetVSMMoments(in float depth)
{
    return float2(depth, depth * depth);
}

float2 ReduceMoments(float2 a, float2 b, float2 c, float2 d)
{
	float2 avg = (a + b + c + d) * 0.25;
	float minDepth = min(min(a.x, b.x), min(c.x, d.x));
	float2 minMoments = GetVSMMoments(minDepth);
	return lerp(avg, minMoments, VSM_MIN_BIAS);
}

groupshared float2 g_scratchDepths[8][8];

#if defined(DOWNSAMPLE_SHADOW_MIP0)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	// Gather from cascade 1 and compute VSM moments
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, 1));

	float2 avg = 0;
	for(uint i = 0; i < 4; i++)
		avg += GetVSMMoments(depths[i]);
	avg *= 0.25;

	float minDepth = min(min(depths.x, depths.y), min(depths.z, depths.w));
	float2 vsmDepth = lerp(avg, GetVSMMoments(minDepth), VSM_MIN_BIAS);

	g_scratchDepths[groupThreadID.x][groupThreadID.y] = vsmDepth;

	GroupMemoryBarrierWithGroupSync();

	// First 2x2 reduction
	if (all((groupThreadID.xy % 2) == 0)) {
		uint2 tid = groupThreadID.xy;
		OutputTexture[dispatchThreadID.xy / 2] = ReduceMoments(
			g_scratchDepths[tid.x + 0][tid.y + 0],
			g_scratchDepths[tid.x + 1][tid.y + 0],
			g_scratchDepths[tid.x + 0][tid.y + 1],
			g_scratchDepths[tid.x + 1][tid.y + 1]);
	}
}

#elif defined(DOWNSAMPLE_SHADOW_MIP1)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	// Gather from cascade 0 and compute VSM moments
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, 0));

	float2 avg = 0;
	for(uint i = 0; i < 4; i++)
		avg += GetVSMMoments(depths[i]);
	avg *= 0.25;

	float minDepth = min(min(depths.x, depths.y), min(depths.z, depths.w));
	float2 vsmDepth = lerp(avg, GetVSMMoments(minDepth), VSM_MIN_BIAS);

	g_scratchDepths[groupThreadID.x][groupThreadID.y] = vsmDepth;

	GroupMemoryBarrierWithGroupSync();

	// First reduction: 2x2
	if (all((groupThreadID.xy % 2) == 0)) {
		uint2 tid = groupThreadID.xy;
		g_scratchDepths[tid.x][tid.y] = ReduceMoments(
			g_scratchDepths[tid.x + 0][tid.y + 0],
			g_scratchDepths[tid.x + 1][tid.y + 0],
			g_scratchDepths[tid.x + 0][tid.y + 1],
			g_scratchDepths[tid.x + 1][tid.y + 1]);
	}

	GroupMemoryBarrierWithGroupSync();

	// Second reduction: 4x4
	if (all((groupThreadID.xy % 4) == 0)) {
		uint2 tid = groupThreadID.xy;
		OutputTexture[dispatchThreadID.xy / 4] = ReduceMoments(
			g_scratchDepths[tid.x + 0][tid.y + 0],
			g_scratchDepths[tid.x + 2][tid.y + 0],
			g_scratchDepths[tid.x + 0][tid.y + 2],
			g_scratchDepths[tid.x + 2][tid.y + 2]);
	}
}
#endif