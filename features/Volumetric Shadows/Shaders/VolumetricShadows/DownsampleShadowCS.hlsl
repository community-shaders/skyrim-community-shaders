Texture2DArray<float> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

//=================================================================================================
//
//  Shadows Sample
//  by MJP
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

float4 GetOptimizedMoments(in float depth)
{
    float square = depth * depth;
    float4 moments = float4(depth, square, square * depth, square * square);
    float4 optimized = mul(moments, float4x4(-2.07224649f,    13.7948857237f,  0.105877704f,   9.7924062118f,
                                              32.23703778f,  -59.4683975703f, -1.9077466311f, -33.7652110555f,
                                             -68.571074599f,  82.0359750338f,  9.3496555107f,  47.9456096605f,
                                              39.3703274134f,-35.364903257f,  -6.6543490743f, -23.9728048165f));
    optimized[0] += 0.035955884801f;
    return optimized;
}

groupshared float4 g_scratchDepths[8][8];

#if defined(DOWNSAMPLE_SHADOW_MIP0)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	// Gather from cascade 1 and compute MSM moments
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, 1));

	float4 msmDepth = 0;
	for(uint i = 0; i < 4; i++)
		msmDepth += GetOptimizedMoments(depths[i]);
	msmDepth *= 0.25;

	g_scratchDepths[groupThreadID.x][groupThreadID.y] = msmDepth;

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
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
	uint2 pixCoord = dispatchThreadID.xy * 2;

	uint inputW, inputH, inputSlices;
	InputTexture.GetDimensions(inputW, inputH, inputSlices);
	float2 uv = (pixCoord + 0.5) / float2(inputW, inputH);

	// Gather from cascade 0 and compute MSM moments
	float4 depths = InputTexture.GatherRed(LinearSampler, float3(uv, 0));

	float4 msmDepth = 0;
	for(uint i = 0; i < 4; i++)
		msmDepth += GetOptimizedMoments(depths[i]);
	msmDepth *= 0.25;

	g_scratchDepths[groupThreadID.x][groupThreadID.y] = msmDepth;

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
