// 11x11 separable Gaussian blur for VSM shadow map
// BLUR_HORIZONTAL - horizontal pass
// BLUR_VERTICAL - vertical pass

Texture2D<float2> InputTexture : register(t0);
RWTexture2D<float2> OutputTexture : register(u0);

// Gaussian weights for 11-tap kernel (sigma ~= 2.5)
static const float weights[6] = {
	0.198596,  // center
	0.175713,  // +/- 1
	0.121703,  // +/- 2
	0.065984,  // +/- 3
	0.028002,  // +/- 4
	0.009302   // +/- 5
};

#define KERNEL_RADIUS 5
#define GROUP_SIZE 128

// Shared memory for efficient loading
// We need GROUP_SIZE + 2 * KERNEL_RADIUS elements
groupshared float2 g_cache[GROUP_SIZE + 2 * KERNEL_RADIUS];

#if defined(BLUR_HORIZONTAL)
[numthreads(GROUP_SIZE, 1, 1)] void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID) {
	uint width, height;
	InputTexture.GetDimensions(width, height);

	int2 baseCoord = int2(groupID.x * GROUP_SIZE - KERNEL_RADIUS, groupID.y);
	int localIdx = groupThreadID.x;

	// Load main data
	int2 coord = baseCoord + int2(localIdx, 0);
	coord.x = clamp(coord.x, 0, (int)width - 1);
	g_cache[localIdx] = InputTexture[coord];

	// Load extra data for kernel overlap
	if (localIdx < 2 * KERNEL_RADIUS) {
		coord = baseCoord + int2(GROUP_SIZE + localIdx, 0);
		coord.x = clamp(coord.x, 0, (int)width - 1);
		g_cache[GROUP_SIZE + localIdx] = InputTexture[coord];
	}

	GroupMemoryBarrierWithGroupSync();

	// Only process valid pixels
	if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
		return;

	// Apply horizontal blur
	float2 result = g_cache[localIdx + KERNEL_RADIUS] * weights[0];

	[unroll] for (int i = 1; i <= KERNEL_RADIUS; i++)
	{
		result += g_cache[localIdx + KERNEL_RADIUS - i] * weights[i];
		result += g_cache[localIdx + KERNEL_RADIUS + i] * weights[i];
	}

	OutputTexture[dispatchThreadID.xy] = result;
}

#elif defined(BLUR_VERTICAL)
[numthreads(1, GROUP_SIZE, 1)] void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID) {
	uint width, height;
	InputTexture.GetDimensions(width, height);

	int2 baseCoord = int2(groupID.x, groupID.y * GROUP_SIZE - KERNEL_RADIUS);
	int localIdx = groupThreadID.y;

	// Load main data
	int2 coord = baseCoord + int2(0, localIdx);
	coord.y = clamp(coord.y, 0, (int)height - 1);
	g_cache[localIdx] = InputTexture[coord];

	// Load extra data for kernel overlap
	if (localIdx < 2 * KERNEL_RADIUS) {
		coord = baseCoord + int2(0, GROUP_SIZE + localIdx);
		coord.y = clamp(coord.y, 0, (int)height - 1);
		g_cache[GROUP_SIZE + localIdx] = InputTexture[coord];
	}

	GroupMemoryBarrierWithGroupSync();

	// Only process valid pixels
	if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
		return;

	// Apply vertical blur
	float2 result = g_cache[localIdx + KERNEL_RADIUS] * weights[0];

	[unroll] for (int i = 1; i <= KERNEL_RADIUS; i++)
	{
		result += g_cache[localIdx + KERNEL_RADIUS - i] * weights[i];
		result += g_cache[localIdx + KERNEL_RADIUS + i] * weights[i];
	}

	OutputTexture[dispatchThreadID.xy] = result;
}
#endif
